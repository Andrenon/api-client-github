/*
 * test_http_client_unit.c — Sprint 3.2, unit tests offline.
 *
 * Analogo a la "Ronda de desarrollo" de informe_validacion.md (Fase 2):
 * ahi el prototipo Python valido la logica de los 4 casos con respuestas
 * HTTP simuladas via unittest.mock, reproduciendo status codes, bodies y
 * headers reales de GitHub, ANTES de confirmarlo en vivo. Ac'a se hace
 * el equivalente en C: se arman HttpResponse a mano (sin pasar por
 * curl_easy_perform) y se llama directo a las funciones puras
 * (http_client_classify, http_client_parse_last_page_from_link,
 * http_client_count_paginated_response), que fueron separadas del
 * transporte real (http_client_get) exactamente para permitir esto -es
 * el reemplazo de unittest.mock en un lenguaje sin mocking framework
 * integrado: en vez de interceptar la llamada de red, se prueba la
 * logica de interpretacion como una funcion pura e independiente de la
 * red.
 *
 * No requiere red ni GITHUB_TOKEN. Se corre con `make test-unit`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_client.h"

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            printf("  [OK] %s\n", msg);                                      \
        } else {                                                             \
            printf("  [FAIL] %s\n", msg);                                    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------- */
/* Helpers para armar un HttpResponse "a mano", sin red                  */
/* ------------------------------------------------------------------- */

static HttpResponse *make_response(long status_code, const char *body) {
    HttpResponse *r = calloc(1, sizeof(HttpResponse));
    r->status_code = status_code;
    r->body = strdup(body != NULL ? body : "");
    r->body_len = strlen(r->body);
    r->headers = NULL;
    r->headers_count = 0;
    return r;
}

static void add_header(HttpResponse *r, const char *name, const char *value) {
    r->headers = realloc(r->headers, (r->headers_count + 1) * sizeof(HttpHeader));
    r->headers[r->headers_count].name = strdup(name);
    r->headers[r->headers_count].value = strdup(value);
    r->headers_count++;
}

/* ------------------------------------------------------------------- */
/* http_client_classify()                                                */
/* ------------------------------------------------------------------- */

static void test_classify_200(void) {
    printf("classify: 200 -> GH_OK\n");
    HttpResponse *r = make_response(200, "{\"id\":1}");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_OK, "200 clasifica como GH_OK");
    http_response_free(r);
}

static void test_classify_401(void) {
    printf("classify: 401 -> GH_ERR_INVALID_TOKEN\n");
    HttpResponse *r = make_response(401, "{\"message\":\"Bad credentials\"}");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_INVALID_TOKEN, "401 clasifica como GH_ERR_INVALID_TOKEN");
    CHECK(err.status_code == 401, "status_code queda en 401");
    http_response_free(r);
}

static void test_classify_404(void) {
    printf("classify: 404 -> GH_ERR_NOT_FOUND\n");
    HttpResponse *r = make_response(404, "{\"message\":\"Not Found\"}");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_NOT_FOUND, "404 clasifica como GH_ERR_NOT_FOUND");
    http_response_free(r);
}

static void test_classify_429_con_reset(void) {
    printf("classify: 429 con X-RateLimit-Reset -> GH_ERR_RATE_LIMIT_EXCEEDED\n");
    HttpResponse *r = make_response(429, "{\"message\":\"secondary rate limit\"}");
    add_header(r, "X-RateLimit-Reset", "1753500000");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_RATE_LIMIT_EXCEEDED, "429 clasifica como GH_ERR_RATE_LIMIT_EXCEEDED");
    CHECK(err.has_reset_at && err.reset_at == 1753500000,
          "reset_at se parsea del header X-RateLimit-Reset");
    http_response_free(r);
}

static void test_classify_403_rate_limit(void) {
    printf("classify: 403 con X-RateLimit-Remaining=0 -> GH_ERR_RATE_LIMIT_EXCEEDED "
           "(no GH_ERR_FORBIDDEN)\n");
    HttpResponse *r = make_response(403, "{\"message\":\"API rate limit exceeded\"}");
    add_header(r, "X-RateLimit-Remaining", "0");
    add_header(r, "X-RateLimit-Reset", "1753500900");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_RATE_LIMIT_EXCEEDED,
          "403 con Remaining=0 clasifica como rate limit, no como forbidden generico");
    CHECK(err.has_reset_at && err.reset_at == 1753500900, "reset_at tambien se toma del 403");
    http_response_free(r);
}

static void test_classify_403_too_large(void) {
    printf("classify: 403 'too large to list' -> GH_ERR_RESOURCE_TOO_LARGE (caso real "
           "torvalds/linux/contributors, ver notas-implementacion.md #1)\n");
    HttpResponse *r = make_response(
        403,
        "{\"message\":\"The history or contributor list is too large to list "
        "contributors for this repository via the API.\",\"documentation_url\":"
        "\"https://docs.github.com/rest/reference/repos#list-repository-contributors\"}");
    /* a proposito SIN X-RateLimit-Remaining: este 403 no es de rate limit */
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_RESOURCE_TOO_LARGE,
          "403 'too large' clasifica como GH_ERR_RESOURCE_TOO_LARGE, no como forbidden");
    http_response_free(r);
}

static void test_classify_403_too_large_uppercase(void) {
    printf("classify: 403 con 'TOO LARGE' en mayusculas -> igual GH_ERR_RESOURCE_TOO_LARGE "
           "(replica el .lower() de Python)\n");
    HttpResponse *r = make_response(403, "{\"message\":\"TOO LARGE to list via API\"}");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_RESOURCE_TOO_LARGE,
          "la comparacion es case-insensitive, igual que body_message.lower() en Python");
    http_response_free(r);
}

static void test_classify_403_forbidden_generico(void) {
    printf("classify: 403 generico (sin rate limit, sin 'too large') -> GH_ERR_FORBIDDEN\n");
    HttpResponse *r = make_response(403, "{\"message\":\"Must have admin rights\"}");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_FORBIDDEN, "403 sin las otras dos senales clasifica como forbidden generico");
    http_response_free(r);
}

static void test_classify_403_body_no_json(void) {
    printf("classify: 403 con body que no es JSON valido -> no explota, cae a "
           "GH_ERR_FORBIDDEN (replica el except ValueError de Python)\n");
    HttpResponse *r = make_response(403, "esto no es json");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_FORBIDDEN, "body no-JSON no crashea, se trata como forbidden generico");
    http_response_free(r);
}

static void test_classify_unexpected(void) {
    printf("classify: 500 -> GH_ERR_UNEXPECTED_STATUS\n");
    HttpResponse *r = make_response(500, "{}");
    GitHubError err;
    GitHubErrorCode code = http_client_classify(r, &err);
    CHECK(code == GH_ERR_UNEXPECTED_STATUS, "500 clasifica como GH_ERR_UNEXPECTED_STATUS");
    CHECK(strstr(err.message, "500") != NULL, "el mensaje incluye el status code");
    http_response_free(r);
}

/* ------------------------------------------------------------------- */
/* http_client_parse_last_page_from_link()                               */
/* ------------------------------------------------------------------- */

static void test_link_parsing(void) {
    printf("Link header parsing:\n");

    /* Caso real (formato exacto documentado por GitHub) */
    const char *link1 =
        "<https://api.github.com/repositories/2325298/branches?page=2&per_page=1>; "
        "rel=\"next\", <https://api.github.com/repositories/2325298/branches?page=3&"
        "per_page=1>; rel=\"last\"";
    CHECK(http_client_parse_last_page_from_link(link1) == 3,
          "extrae page=3 de rel=\"last\" con next+last presentes");

    /* Caso trampa: per_page aparece ANTES que page en el query string.
     * "per_page=1" contiene la subcadena "page=1": un strstr(url,"page=")
     * naive matchearia ahi y devolveria 1 en vez de 723. */
    const char *link2 =
        "<https://api.github.com/repositories/2325298/contributors?per_page=1&page=2>; "
        "rel=\"next\", <https://api.github.com/repositories/2325298/contributors?"
        "per_page=1&page=723>; rel=\"last\"";
    CHECK(http_client_parse_last_page_from_link(link2) == 723,
          "no confunde 'per_page=1' con 'page=1' cuando per_page va antes que page");

    /* Sin rel="last" (no deberia pasar en la practica, pero no debe
     * crashear ni devolver cualquier cosa) */
    const char *link3 =
        "<https://api.github.com/repositories/2325298/branches?page=1&per_page=1>; "
        "rel=\"next\"";
    CHECK(http_client_parse_last_page_from_link(link3) == -1,
          "sin rel=\"last\" devuelve -1 (no encontrado)");

    CHECK(http_client_parse_last_page_from_link(NULL) == -1, "NULL devuelve -1, no crashea");
    CHECK(http_client_parse_last_page_from_link("") == -1, "string vacio devuelve -1");
}

/* ------------------------------------------------------------------- */
/* http_client_count_paginated_response() — fallback (sin header Link)   */
/* ------------------------------------------------------------------- */

static void test_count_fallback(void) {
    printf("count_paginated_response (fallback, sin header Link):\n");

    /* Caso real torvalds/linux/releases: 0 elementos, sin Link */
    HttpResponse *empty = make_response(200, "[]");
    CHECK(http_client_count_paginated_response(empty) == 0,
          "array vacio sin Link -> cuenta 0 (caso real releases de torvalds/linux)");
    http_response_free(empty);

    /* Caso real torvalds/linux/branches: 1 elemento, sin Link */
    HttpResponse *one = make_response(200, "[{\"name\":\"master\"}]");
    CHECK(http_client_count_paginated_response(one) == 1,
          "array de 1 elemento sin Link -> cuenta 1 (caso real branches de torvalds/linux)");
    http_response_free(one);

    /* Con header Link presente, el fallback ni se toca */
    HttpResponse *paged = make_response(200, "[{\"name\":\"master\"}]");
    add_header(paged, "Link",
               "<https://api.github.com/x?page=3&per_page=1>; rel=\"last\"");
    CHECK(http_client_count_paginated_response(paged) == 3,
          "con header Link presente, usa el Link (3) e ignora el body");
    http_response_free(paged);

    /* Body invalido -> -1, no crashea */
    HttpResponse *bad = make_response(200, "esto no es un array json");
    CHECK(http_client_count_paginated_response(bad) == -1,
          "body no-JSON-array devuelve -1 en vez de crashear");
    http_response_free(bad);
}

/* ------------------------------------------------------------------- */

int main(void) {
    printf("=== Unit tests: Sprint 3.2 (http_client, offline) ===\n\n");

    test_classify_200();
    test_classify_401();
    test_classify_404();
    test_classify_429_con_reset();
    test_classify_403_rate_limit();
    test_classify_403_too_large();
    test_classify_403_too_large_uppercase();
    test_classify_403_forbidden_generico();
    test_classify_403_body_no_json();
    test_classify_unexpected();
    printf("\n");
    test_link_parsing();
    printf("\n");
    test_count_fallback();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

