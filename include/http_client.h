#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * http_client.h — cliente HTTP generico + interpretacion de errores +
 * rate limiting (Sprint 3.2). Replica http_client.py del prototipo.
 *
 * Decision de diseno principal: Python modela los errores con una
 * jerarquia de excepciones (GitHubAPIError y subclases). C no tiene
 * excepciones. La alternativa no es "simular" excepciones con setjmp/
 * longjmp (complica el control de flujo y el manejo de recursos para un
 * beneficio dudoso) sino aplanar la jerarquia a un enum (GitHubErrorCode)
 * + un struct de detalle (GitHubError) que el caller recibe por
 * out-parameter. Es mas verboso en cada call site (hay que chequear el
 * codigo de retorno a mano en vez de que la excepcion suba sola), pero
 * es el patron idiomatico en C y el compilador al menos fuerza a que el
 * codigo de retorno exista (no a que se chequee, eso queda en el
 * programador, como con cualquier funcion en C).
 *
 * Segunda decision importante: separar "hacer el pedido HTTP"
 * (http_client_get) de "interpretar la respuesta" (http_client_classify)
 * en dos funciones en vez de una sola. Esto es lo que en Python se
 * resuelve gratis con unittest.mock (informe_validacion.md, "Ronda de
 * desarrollo"): al ser http_client_classify() una funcion pura que solo
 * mira un HttpResponse ya armado, se puede testear cada rama
 * (401/403 triple/404/429/inesperado) construyendo un HttpResponse a
 * mano, sin tocar la red ni necesitar un framework de mocking.
 */

#define GITHUB_API_BASE_URL "https://api.github.com"
#define HTTP_CLIENT_USER_AGENT "api-client-github"
#define HTTP_CLIENT_DEFAULT_TIMEOUT_SECONDS 10L

typedef enum {
    GH_OK = 0,
    GH_ERR_NETWORK,           /* fallo de curl: DNS, timeout, TLS, etc. */
    GH_ERR_INVALID_TOKEN,     /* 401 */
    GH_ERR_FORBIDDEN,         /* 403 generico (no rate limit, no "too large") */
    GH_ERR_RESOURCE_TOO_LARGE,/* 403 "too large to list..." (ver notas-implementacion.md #1) */
    GH_ERR_NOT_FOUND,         /* 404 */
    GH_ERR_RATE_LIMIT_EXCEEDED, /* 429, o 403 con X-RateLimit-Remaining: 0 */
    GH_ERR_UNEXPECTED_STATUS, /* cualquier otro status no contemplado */
    GH_ERR_INTERNAL,          /* malloc fallido, JSON invalido donde no deberia, etc. */
} GitHubErrorCode;

/*
 * Detalle de un error. message es un buffer de tamano fijo (no char* +
 * malloc): todos los mensajes son literales cortos o un literal con un
 * numero de status formateado, asi que no hace falta reserva dinamica.
 * Consecuencia directa: GitHubError es "POD" (Plain Old Data, sin punteros propios)
 * y no necesita una funcion de liberacion, a diferencia de HttpResponse.
 */
typedef struct {
    GitHubErrorCode code;
    long status_code;   /* 0 si nunca hubo respuesta HTTP (error de red) */
    char message[160];
    bool has_reset_at;
    int64_t reset_at;   /* epoch de X-RateLimit-Reset; valido solo si has_reset_at */
} GitHubError;

typedef struct {
    char *name;  /* owned */
    char *value; /* owned */
} HttpHeader;

typedef struct {
    long status_code;
    char *body;           /* owned, siempre NUL-terminated (nunca NULL) */
    size_t body_len;
    HttpHeader *headers;  /* owned array */
    size_t headers_count;
} HttpResponse;

/* Se llaman una unica vez desde main() (Sprint 3.6), no por cada
 * request: curl_global_init() no es thread-safe para llamar repetidas
 * veces en paralelo, y esta app ademas es single-threaded, asi que basta
 * con un init/cleanup por ejecucion del proceso. */
bool http_client_global_init(void);
void http_client_global_cleanup(void);

void http_response_free(HttpResponse *response);

/*
 * Puebla un GitHubError a mano. Pensado para que otros modulos
 * (json_parser.c/db.c/core.c) reporten sus propios errores
 * internos con el mismo tipo que usa todo el resto del pipeline, 
 * en vez de que cada .c reinvente su propio helper de armado
 * de errores. status_code puede ser 0 si el error no viene de una
 * respuesta HTTP (ej. "sin memoria", "JSON con forma inesperada").
 */
void github_error_set(GitHubError *out_error, GitHubErrorCode code, long status_code,
                       const char *message);

/* Busca un header case-insensitive (asi los define HTTP). Devuelve un
 * puntero PRESTADO hacia dentro de response->headers (no liberar); NULL
 * si no esta. */
const char *http_response_get_header(const HttpResponse *response, const char *name);

/*
 * Ejecuta el GET real contra la red.
 *   path:  relativo a GITHUB_API_BASE_URL, ej "/repos/torvalds/linux".
 *   token: puede ser NULL -> se intenta GITHUB_TOKEN de entorno (mismo
 *          fallback que _build_headers() en el prototipo).
 *   query: query string sin el '?', ej "per_page=1", o NULL.
 *
 * Devuelve true + *out_response poblado (con CUALQUIER status code, no
 * solo 200) si el transporte funciono. Devuelve false + *out_error
 * poblado (GH_ERR_NETWORK) si curl fallo (DNS/timeout/TLS/etc.) -en ese
 * caso *out_response queda en NULL.
 *
 * Deliberadamente NO clasifica el status code: eso es
 * http_client_classify(), aparte.
 */
bool http_client_get(const char *path, const char *token, const char *query,
                      HttpResponse **out_response, GitHubError *out_error);

/*
 * Interpreta un HttpResponse ya obtenido, replicando _raise_for_status()
 * del prototipo: 200 -> GH_OK; 401/404/429 directo; 403 con los tres
 * significados (rate limit / "too large" / forbidden generico, ver
 * tests/prototype/informe_validacion.md); cualquier otro status ->
 * GH_ERR_UNEXPECTED_STATUS. Funcion pura (no toca la red), pensada para
 * poder testearse con un HttpResponse armado a mano.
 */
GitHubErrorCode http_client_classify(const HttpResponse *response, GitHubError *out_error);

/*
 * Combina http_client_get() + http_client_classify(): es lo que van a
 * usar los modulos de endpoints (Sprint 3.3 en adelante). Si el status
 * no es 200, libera el HttpResponse antes de devolver el error (el
 * caller solo se queda con el detalle en out_error).
 */
GitHubErrorCode http_client_get_and_classify(const char *path, const char *token,
                                              const char *query,
                                              HttpResponse **out_response,
                                              GitHubError *out_error);

/*
 * Dado el valor crudo de un header Link (formato RFC 8288, el que usa
 * GitHub para paginacion), devuelve el numero de pagina de rel="last",
 * o -1 si no esta presente (caso de 0 o 1 pagina total). Funcion pura.
 *
 * Nota: "per_page=1" contiene la subcadena "page=1" -una busqueda naive
 * de "page=" en la URL matchearia ahi por error si per_page apareciera
 * antes que page en el query string-. Por eso se busca especificamente
 * "?page=" o "&page=", nunca un "page=" suelto.
 */
long http_client_parse_last_page_from_link(const char *link_header_value);

/*
 * Cuenta el total de elementos de un HttpResponse ya obtenido (status
 * 200) de un endpoint paginado: usa el header Link si esta, o cuenta el
 * array JSON del body si no (fallback: 0 o 1 elementos, ver
 * docs/notas-implementacion.md). Devuelve -1 si el body no resulta ser
 * un array JSON valido (no deberia pasar contra la API real).
 */
long http_client_count_paginated_response(const HttpResponse *response);

/*
 * Replica get_paginated_count() del prototipo: GET con per_page=1 +
 * http_client_count_paginated_response(). Usado para contributors,
 * releases y branches (ver docs/notas-implementacion.md, punto sobre el
 * truco per_page=1 + header Link).
 */
GitHubErrorCode http_client_get_paginated_count(const char *path, const char *token,
                                                 long *out_count, GitHubError *out_error);

typedef struct {
    int64_t limit;
    int64_t remaining;
    int64_t reset; /* epoch */
} RateLimitStatus;

/*
 * GET /rate_limit. Replica get_rate_limit_status(): este endpoint NO
 * cuenta contra su propio limite -confirmado empiricamente contra la API
 * real, ver docs/notas-implementacion.md #3- asi que es seguro llamarlo
 * siempre, incluso justo despues de un 429/403.
 */
GitHubErrorCode http_client_get_rate_limit_status(const char *token,
                                                   RateLimitStatus *out_status,
                                                   GitHubError *out_error);

#endif /* HTTP_CLIENT_H */

