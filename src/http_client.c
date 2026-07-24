#include "http_client.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp: vive aca, no en string.h */

/* --------------------------------------------------------------------- */
/* Helpers de error                                                       */
/* --------------------------------------------------------------------- */

static void set_error(GitHubError *out_error, GitHubErrorCode code, long status_code,
                      const char *message) {
    if (out_error == NULL) {
        return;
    }
    out_error->code = code;
    out_error->status_code = status_code;
    snprintf(out_error->message, sizeof(out_error->message), "%s", message);
    out_error->has_reset_at = false;
    out_error->reset_at = 0;
}

static void set_reset_at_from_header(GitHubError *out_error, const HttpResponse *response) {
    if (out_error == NULL) {
        return;
    }
    const char *reset_str = http_response_get_header(response, "X-RateLimit-Reset");
    if (reset_str == NULL) {
        return;
    }
    char *endptr = NULL;
    long long value = strtoll(reset_str, &endptr, 10);
    if (endptr != reset_str) {
        out_error->has_reset_at = true;
        out_error->reset_at = (int64_t)value;
    }
}

/* --------------------------------------------------------------------- */
/* Helpers de JSON minimos (solo lo generico que necesita este modulo:    */
/* leer un campo string y contar un array. La extraccion de campos hacia  */
/* el esquema consolidado -RepoInfo/Language- es responsabilidad de       */
/* json_parser.c, Sprint 3.3; esto de aca es deliberadamente generico).   */
/* --------------------------------------------------------------------- */

static char *extract_json_string_field(const char *json_body, const char *field_name) {
    if (json_body == NULL) {
        return NULL;
    }
    cJSON *root = cJSON_Parse(json_body);
    if (root == NULL) {
        return NULL;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field_name);
    char *result = NULL;
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        result = strdup(item->valuestring);
    }
    cJSON_Delete(root);
    return result;
}

/* No se usa strcasestr (extension GNU/BSD, fuera de POSIX.1-2008) para
 * no depender de _GNU_SOURCE: se hace una copia en minuscula y se busca
 * con strstr, replicando exactamente la semantica de
 * `body_message.lower()` en el prototipo Python. */
static char *str_to_lower_copy(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        copy[i] = (char)tolower((unsigned char)s[i]);
    }
    copy[len] = '\0';
    return copy;
}

/* --------------------------------------------------------------------- */
/* Clasificacion de errores (funcion pura, testeable sin red)             */
/* --------------------------------------------------------------------- */

GitHubErrorCode http_client_classify(const HttpResponse *response, GitHubError *out_error) {
    long status = response->status_code;

    if (status == 200) {
        set_error(out_error, GH_OK, status, "");
        return GH_OK;
    }

    if (status == 401) {
        set_error(out_error, GH_ERR_INVALID_TOKEN, status, "Token invalido o expirado (401).");
        return GH_ERR_INVALID_TOKEN;
    }

    if (status == 404) {
        set_error(out_error, GH_ERR_NOT_FOUND, status,
                  "Repositorio inexistente o no accesible (404).");
        return GH_ERR_NOT_FOUND;
    }

    if (status == 429) {
        set_error(out_error, GH_ERR_RATE_LIMIT_EXCEEDED, status,
                  "Demasiadas solicitudes, limite de tasa excedido (429).");
        set_reset_at_from_header(out_error, response);
        return GH_ERR_RATE_LIMIT_EXCEEDED;
    }

    if (status == 403) {
        const char *remaining = http_response_get_header(response, "X-RateLimit-Remaining");
        if (remaining != NULL && strcmp(remaining, "0") == 0) {
            set_error(out_error, GH_ERR_RATE_LIMIT_EXCEEDED, status,
                      "Limite de tasa excedido (403 con X-RateLimit-Remaining=0).");
            set_reset_at_from_header(out_error, response);
            return GH_ERR_RATE_LIMIT_EXCEEDED;
        }

        char *body_message = extract_json_string_field(response->body, "message");
        bool too_large = false;
        if (body_message != NULL) {
            char *lower = str_to_lower_copy(body_message);
            if (lower != NULL) {
                too_large = strstr(lower, "too large") != NULL;
                free(lower);
            }
            free(body_message);
        }

        if (too_large) {
            set_error(out_error, GH_ERR_RESOURCE_TOO_LARGE, status,
                      "El repositorio tiene demasiado historial para que GitHub calcule "
                      "este listado via API (403).");
            return GH_ERR_RESOURCE_TOO_LARGE;
        }

        set_error(out_error, GH_ERR_FORBIDDEN, status, "Acceso prohibido (403).");
        return GH_ERR_FORBIDDEN;
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "Respuesta inesperada de la API: %ld", status);
    set_error(out_error, GH_ERR_UNEXPECTED_STATUS, status, msg);
    return GH_ERR_UNEXPECTED_STATUS;
}

/* --------------------------------------------------------------------- */
/* Header Link (paginacion) — funciones puras, testeables sin red         */
/* --------------------------------------------------------------------- */

/* Busca especificamente "?page=" o "&page=" (nunca un "page=" suelto):
 * "per_page=1" contiene la subcadena "page=1", asi que un strstr naive
 * de "page=" matchearia ahi por error si per_page aparece antes que page
 * en el query string de la URL (el orden no esta garantizado). */
static long extract_page_query_param(const char *url) {
    static const char *candidates[] = {"?page=", "&page="};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const char *pos = strstr(url, candidates[i]);
        if (pos != NULL) {
            return strtol(pos + strlen(candidates[i]), NULL, 10);
        }
    }
    return -1;
}

long http_client_parse_last_page_from_link(const char *link_header_value) {
    if (link_header_value == NULL) {
        return -1;
    }

    char *copy = strdup(link_header_value);
    if (copy == NULL) {
        return -1;
    }

    long result = -1;
    char *saveptr = NULL;
    char *segment = strtok_r(copy, ",", &saveptr);

    while (segment != NULL) {
        if (strstr(segment, "rel=\"last\"") != NULL) {
            char *url_start = strchr(segment, '<');
            char *url_end = strchr(segment, '>');
            if (url_start != NULL && url_end != NULL && url_end > url_start + 1) {
                size_t url_len = (size_t)(url_end - url_start - 1);
                char *url = malloc(url_len + 1);
                if (url != NULL) {
                    memcpy(url, url_start + 1, url_len);
                    url[url_len] = '\0';
                    result = extract_page_query_param(url);
                    free(url);
                }
            }
            break;
        }
        segment = strtok_r(NULL, ",", &saveptr);
    }

    free(copy);
    return result;
}

long http_client_count_paginated_response(const HttpResponse *response) {
    const char *link = http_response_get_header(response, "Link");
    if (link != NULL) {
        long last_page = http_client_parse_last_page_from_link(link);
        if (last_page >= 0) {
            return last_page;
        }
    }

    /* Fallback: no hay header Link -> 0 o 1 elementos totales (ver
     * tests/prototype/informe_validacion.md). Se cuenta el array JSON del body
     * directamente con cJSON_GetArraySize: es una operacion generica de
     * "cuantos elementos tiene este array", no una extraccion de campos
     * especificos de dominio, asi que no invade el rol de json_parser.c
     * (Sprint 3.3). */
    cJSON *root = cJSON_Parse(response->body);
    if (root == NULL || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return -1;
    }
    long count = (long)cJSON_GetArraySize(root);
    cJSON_Delete(root);
    return count;
}

/* --------------------------------------------------------------------- */
/* Headers: lookup y builder                                              */
/* --------------------------------------------------------------------- */

const char *http_response_get_header(const HttpResponse *response, const char *name) {
    if (response == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < response->headers_count; i++) {
        if (strcasecmp(response->headers[i].name, name) == 0) {
            return response->headers[i].value;
        }
    }
    return NULL;
}

typedef struct {
    HttpHeader *items;
    size_t count;
    size_t capacity;
} HeaderBuilder;

static bool header_builder_push(HeaderBuilder *builder, const char *name, size_t name_len,
                                 const char *value, size_t value_len) {
    if (builder->count == builder->capacity) {
        size_t new_capacity = builder->capacity == 0 ? 8 : builder->capacity * 2;
        HttpHeader *bigger = realloc(builder->items, new_capacity * sizeof(HttpHeader));
        if (bigger == NULL) {
            return false;
        }
        builder->items = bigger;
        builder->capacity = new_capacity;
    }

    char *name_copy = malloc(name_len + 1);
    char *value_copy = malloc(value_len + 1);
    if (name_copy == NULL || value_copy == NULL) {
        free(name_copy);
        free(value_copy);
        return false;
    }
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    memcpy(value_copy, value, value_len);
    value_copy[value_len] = '\0';

    builder->items[builder->count].name = name_copy;
    builder->items[builder->count].value = value_copy;
    builder->count++;
    return true;
}

static void header_builder_free(HeaderBuilder *builder) {
    for (size_t i = 0; i < builder->count; i++) {
        free(builder->items[i].name);
        free(builder->items[i].value);
    }
    free(builder->items);
    builder->items = NULL;
    builder->count = 0;
    builder->capacity = 0;
}

/* CURLOPT_HEADERFUNCTION: libcurl llama esto linea por linea, incluyendo
 * la status-line ("HTTP/1.1 200 OK") y la linea vacia que separa
 * bloques de headers -ninguna de las dos tiene ':', se ignoran. */
static size_t header_write_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t total = size * nitems;
    HeaderBuilder *builder = (HeaderBuilder *)userdata;

    size_t len = total;
    while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n')) {
        len--;
    }
    if (len == 0) {
        return total;
    }

    char *colon = memchr(buffer, ':', len);
    if (colon == NULL) {
        return total; /* status-line, no es un header Name: Value */
    }

    size_t name_len = (size_t)(colon - buffer);
    const char *value_start = colon + 1;
    size_t value_len = len - name_len - 1;
    while (value_len > 0 && *value_start == ' ') {
        value_start++;
        value_len--;
    }

    if (!header_builder_push(builder, buffer, name_len, value_start, value_len)) {
        return 0; /* señal de error a libcurl: aborta la transferencia */
    }
    return total;
}

/* --------------------------------------------------------------------- */
/* Body: buffer dinamico                                                  */
/* --------------------------------------------------------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} DynBuffer;

static size_t body_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t add = size * nmemb;
    DynBuffer *buf = (DynBuffer *)userdata;

    if (buf->len + add + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? 4096 : buf->capacity * 2;
        while (new_capacity < buf->len + add + 1) {
            new_capacity *= 2;
        }
        char *bigger = realloc(buf->data, new_capacity);
        if (bigger == NULL) {
            return 0; /* aborta la transferencia */
        }
        buf->data = bigger;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->len, ptr, add);
    buf->len += add;
    buf->data[buf->len] = '\0';
    return add;
}

/* --------------------------------------------------------------------- */
/* Construccion de URL y headers de la request                            */
/* --------------------------------------------------------------------- */

static char *build_url(const char *path, const char *query) {
    size_t base_len = strlen(GITHUB_API_BASE_URL) + strlen(path);
    size_t query_len = query != NULL ? strlen(query) + 1 : 0; /* +1 por '?' */
    size_t total = base_len + query_len + 1;                  /* +1 por NUL */

    char *url = malloc(total);
    if (url == NULL) {
        return NULL;
    }
    if (query != NULL) {
        snprintf(url, total, "%s%s?%s", GITHUB_API_BASE_URL, path, query);
    } else {
        snprintf(url, total, "%s%s", GITHUB_API_BASE_URL, path);
    }
    return url;
}

/* Replica _build_headers() del prototipo: User-Agent, Accept, y
 * Authorization opcional (parametro o GITHUB_TOKEN de entorno). */
static struct curl_slist *build_request_headers(const char *token) {
    struct curl_slist *list = NULL;
    list = curl_slist_append(list, "User-Agent: " HTTP_CLIENT_USER_AGENT);
    list = curl_slist_append(list, "Accept: application/vnd.github+json");

    const char *effective_token = token != NULL ? token : getenv("GITHUB_TOKEN");
    if (effective_token != NULL && effective_token[0] != '\0') {
        size_t needed = strlen("Authorization: Bearer ") + strlen(effective_token) + 1;
        char *auth_header = malloc(needed);
        if (auth_header != NULL) {
            snprintf(auth_header, needed, "Authorization: Bearer %s", effective_token);
            /* curl_slist_append copia el string internamente (hace su
             * propio strdup), asi que liberar auth_header ahora mismo es
             * correcto -no se lo esta "reteniendo" con vida corta. */
            list = curl_slist_append(list, auth_header);
            free(auth_header);
        }
    }
    return list;
}

/* --------------------------------------------------------------------- */
/* API publica: transporte                                                */
/* --------------------------------------------------------------------- */

bool http_client_global_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void http_client_global_cleanup(void) {
    curl_global_cleanup();
}

void http_response_free(HttpResponse *response) {
    if (response == NULL) {
        return;
    }
    free(response->body);
    for (size_t i = 0; i < response->headers_count; i++) {
        free(response->headers[i].name);
        free(response->headers[i].value);
    }
    free(response->headers);
    free(response);
}

bool http_client_get(const char *path, const char *token, const char *query,
                      HttpResponse **out_response, GitHubError *out_error) {
    *out_response = NULL;

    char *url = build_url(path, query);
    if (url == NULL) {
        set_error(out_error, GH_ERR_INTERNAL, 0, "No se pudo construir la URL (sin memoria).");
        return false;
    }

    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        free(url);
        set_error(out_error, GH_ERR_INTERNAL, 0, "curl_easy_init() fallo.");
        return false;
    }

    struct curl_slist *header_list = build_request_headers(token);
    DynBuffer body = {0};
    HeaderBuilder headers = {0};

    // 1. Construyendo los parámetros de la Capa de Aplicación HTTP
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_CLIENT_DEFAULT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    // 2. DNS, Sockets, Triple Handshake y Petición (La "Caja Negra" de libcurl)
    CURLcode result = curl_easy_perform(curl);

    // Si curl_easy_perform() no termina con éxito, mensaje de error, libera memoria y finaliza con error
    if (result != CURLE_OK) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Error de red al consultar la API de GitHub: %s",
                 curl_easy_strerror(result));
        set_error(out_error, GH_ERR_NETWORK, 0, msg);

        free(body.data);
        header_builder_free(&headers);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        free(url);
        return false;
    }

    // Se pide a libcurl el código HTTP que devolvió el servidor (ej. 200 OK, 404 Not Found)
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    // Cierre del Socket
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    free(url);

    HttpResponse *response = malloc(sizeof(HttpResponse));
    if (response == NULL) {
        free(body.data);
        header_builder_free(&headers);
        set_error(out_error, GH_ERR_INTERNAL, 0, "Sin memoria al armar HttpResponse.");
        return false;
    }

    // 3. El Dato (Recepción a nivel Capa de Transporte/Aplicación)
    response->status_code = status_code;
    response->body = body.data;
    response->body_len = body.len;
    if (response->body == NULL) {
        response->body = strdup(""); /* nunca dejar body en NULL (ver contrato del header) */
        response->body_len = 0;
    }
    response->headers = headers.items;
    response->headers_count = headers.count;

    *out_response = response;
    return true;
}

GitHubErrorCode http_client_get_and_classify(const char *path, const char *token,
                                              const char *query, HttpResponse **out_response,
                                              GitHubError *out_error) {
    HttpResponse *response = NULL;
    *out_response = NULL;

    if (!http_client_get(path, token, query, &response, out_error)) {
        return GH_ERR_NETWORK;
    }

    GitHubErrorCode result = http_client_classify(response, out_error);
    if (result != GH_OK) {
        http_response_free(response);
        return result;
    }

    *out_response = response;
    return GH_OK;
}

GitHubErrorCode http_client_get_paginated_count(const char *path, const char *token,
                                                 long *out_count, GitHubError *out_error) {
    HttpResponse *response = NULL;
    GitHubErrorCode result =
        http_client_get_and_classify(path, token, "per_page=1", &response, out_error);
    if (result != GH_OK) {
        return result;
    }

    long count = http_client_count_paginated_response(response);
    http_response_free(response);

    if (count < 0) {
        set_error(out_error, GH_ERR_INTERNAL, 0,
                  "No se pudo interpretar la respuesta paginada como un array JSON.");
        return GH_ERR_INTERNAL;
    }

    *out_count = count;
    return GH_OK;
}

GitHubErrorCode http_client_get_rate_limit_status(const char *token, RateLimitStatus *out_status,
                                                   GitHubError *out_error) {
    HttpResponse *response = NULL;
    GitHubErrorCode result =
        http_client_get_and_classify("/rate_limit", token, NULL, &response, out_error);
    if (result != GH_OK) {
        return result;
    }

    cJSON *root = cJSON_Parse(response->body);
    bool ok = root != NULL;
    if (ok) {
        cJSON *resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
        cJSON *core = resources != NULL ? cJSON_GetObjectItemCaseSensitive(resources, "core")
                                         : NULL;
        cJSON *limit = core != NULL ? cJSON_GetObjectItemCaseSensitive(core, "limit") : NULL;
        cJSON *remaining =
            core != NULL ? cJSON_GetObjectItemCaseSensitive(core, "remaining") : NULL;
        cJSON *reset = core != NULL ? cJSON_GetObjectItemCaseSensitive(core, "reset") : NULL;

        ok = cJSON_IsNumber(limit) && cJSON_IsNumber(remaining) && cJSON_IsNumber(reset);
        if (ok) {
            out_status->limit = (int64_t)limit->valuedouble;
            out_status->remaining = (int64_t)remaining->valuedouble;
            out_status->reset = (int64_t)reset->valuedouble;
        }
    }

    cJSON_Delete(root);
    http_response_free(response);

    if (!ok) {
        set_error(out_error, GH_ERR_INTERNAL, 0,
                  "Respuesta de /rate_limit no tiene el shape esperado (resources.core).");
        return GH_ERR_INTERNAL;
    }

    return GH_OK;
}

