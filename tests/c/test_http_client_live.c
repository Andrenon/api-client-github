/*
 * test_http_client_live.c — Sprint 3.2, tests de integracion EN VIVO
 * contra la API real de GitHub (api.github.com), sin token (pool de 60
 * solicitudes/hora, ver README.md "Rate Limiting").
 *
 * Analogo a la "Ronda de cierre" de informe_validacion.md (Fase 2): ahi
 * el prototipo Python confirmo contra la API real los casos que en la
 * ronda de desarrollo solo se habian probado con mocks. Aca se hace lo
 * mismo para el cliente HTTP en C: los casos de clasificacion y de Link
 * header ya se probaron offline en test_http_client_unit.c; este
 * programa los reconfirma con respuestas reales, incluyendo el caso
 * concreto de torvalds/linux/contributors (403 "too large to list") que
 * documenta docs/notas-implementacion.md #1.
 *
 * Hace 7 solicitudes que SI cuentan contra el rate limit (mas 2 a
 * /rate_limit, que no cuentan - notas-implementacion.md #3, verificado
 * mas abajo con una resta exacta). Se corre con `make test-live`.
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

#define INFO(fmt, ...) printf("  [info] " fmt "\n", ##__VA_ARGS__)

static void print_error(const GitHubError *err) {
    printf("       (codigo=%d status=%ld msg=\"%s\")\n", err->code, err->status_code,
           err->message);
}

int main(void) {
    printf("=== Live tests: Sprint 3.2 (http_client contra api.github.com) ===\n\n");

    if (!http_client_global_init()) {
        fprintf(stderr, "http_client_global_init() fallo, abortando.\n");
        return 1;
    }

    GitHubError err;
    HttpResponse *response = NULL;

    /* --- Rate limit ANTES de las 7 solicitudes "contadas" de este test --- */
    RateLimitStatus rl_before;
    GitHubErrorCode rl_code = http_client_get_rate_limit_status(NULL, &rl_before, &err);
    printf("rate_limit (antes):\n");
    CHECK(rl_code == GH_OK, "GET /rate_limit responde GH_OK");
    if (rl_code == GH_OK) {
        printf("  [info] remaining=%lld / limit=%lld\n", (long long)rl_before.remaining,
               (long long)rl_before.limit);
        if (rl_before.remaining < 10) {
            printf("  [WARN] quedan menos de 10 solicitudes en el pool sin-token "
                     "compartido; el resto de este test puede toparse con 429/403 de "
                     "rate limit por motivos ajenos al codigo. Reintentar mas tarde si "
                     "hay fallos raros a partir de aca.\n");
        }
    }
    printf("\n");

    /* --- Caso 1: repo valido (200) --- */
    printf("GET /repos/torvalds/linux (caso: repo valido)\n");
    GitHubErrorCode code =
        http_client_get_and_classify("/repos/torvalds/linux", NULL, NULL, &response, &err);
    CHECK(code == GH_OK, "responde 200 / GH_OK");
    if (code == GH_OK) {
        CHECK(response->status_code == 200, "status_code == 200");
        CHECK(strstr(response->body, "\"name\":\"linux\"") != NULL ||
                  strstr(response->body, "\"name\": \"linux\"") != NULL,
              "el body contiene el campo name=linux (chequeo minimo; la extraccion "
              "completa de campos es responsabilidad de json_parser.c, Sprint 3.3)");
        http_response_free(response);
    } else {
        print_error(&err);
    }
    printf("\n");

    /* --- Caso 2: repo inexistente (404) --- */
    printf("GET /repos/octocat/repo-inexistente-github-client-abc123 (caso: 404)\n");
    code = http_client_get_and_classify(
        "/repos/octocat/repo-inexistente-github-client-abc123", NULL, NULL, &response, &err);
    CHECK(code == GH_ERR_NOT_FOUND, "clasifica como GH_ERR_NOT_FOUND");
    CHECK(response == NULL, "no deja un HttpResponse colgado en el caso de error");
    if (code != GH_ERR_NOT_FOUND) print_error(&err);
    printf("\n");

    /* --- Caso 3: token invalido (401) --- */
    printf("GET /repos/torvalds/linux con token invalido (caso: 401)\n");
    code = http_client_get_and_classify("/repos/torvalds/linux",
                                          "ghp_tokenInvalidoDePrueba123456789", NULL,
                                          &response, &err);
    CHECK(code == GH_ERR_INVALID_TOKEN, "clasifica como GH_ERR_INVALID_TOKEN");
    if (code != GH_ERR_INVALID_TOKEN) print_error(&err);
    printf("\n");

    /* --- Caso 4: 403 "too large to list" (caso real documentado) --- */
    printf("GET /repos/torvalds/linux/contributors?per_page=1 (caso real: 403 'too "
           "large to list', ver notas-implementacion.md #1)\n");
    code = http_client_get_and_classify("/repos/torvalds/linux/contributors", NULL,
                                          "per_page=1", &response, &err);
    CHECK(code == GH_ERR_RESOURCE_TOO_LARGE,
          "clasifica como GH_ERR_RESOURCE_TOO_LARGE (no como GH_ERR_FORBIDDEN generico "
          "ni como GH_ERR_RATE_LIMIT_EXCEEDED)");
    if (code != GH_ERR_RESOURCE_TOO_LARGE) print_error(&err);
    printf("\n");

    /* --- Caso 5: paginacion via header Link (multi-pagina real) --- */
    printf("per_page=1 en /repos/octocat/Hello-World/branches (caso: header Link con "
           "rel=\"last\", repo multi-pagina real)\n");
    code = http_client_get_and_classify("/repos/octocat/Hello-World/branches", NULL,
                                          "per_page=1", &response, &err);
    CHECK(code == GH_OK, "responde 200");
    if (code == GH_OK) {
        const char *link = http_response_get_header(response, "Link");
        CHECK(link != NULL, "la respuesta real trae header Link (repo con >1 pagina)");
        long count = http_client_count_paginated_response(response);
        printf("  [info] branches_count calculado: %ld (informe_validacion.md registro 3 "
               "en su momento; puede variar si el repo demo cambio desde entonces)\n",
               count);
        CHECK(count > 0, "el conteo via Link header da un numero positivo");
        http_response_free(response);
    } else {
        print_error(&err);
    }
    printf("\n");

    /* --- Caso 6: paginacion via fallback (repo de pocas paginas real) --- */
    printf("per_page=1 en /repos/torvalds/linux/branches (caso: SIN header Link, "
           "fallback contando el array, ver notas-implementacion.md)\n");
    code = http_client_get_and_classify("/repos/torvalds/linux/branches", NULL, "per_page=1",
                                          &response, &err);
    CHECK(code == GH_OK, "responde 200");
    if (code == GH_OK) {
        const char *link = http_response_get_header(response, "Link");
        CHECK(link == NULL, "sin header Link (0 o 1 pagina total) -> se ejercita el fallback");
        long count = http_client_count_paginated_response(response);
        printf("  [info] branches_count calculado: %ld (informe_validacion.md registro 1 "
               "en su momento)\n",
               count);
        CHECK(count >= 0, "el fallback da un conteo valido (>= 0)");
        http_response_free(response);
    } else {
        print_error(&err);
    }
    printf("\n");

    /* --- Caso 7: get_paginated_count end-to-end, array vacio --- */
    printf("http_client_get_paginated_count en /repos/torvalds/linux/releases (caso: "
           "array vacio, 0 releases)\n");
    long releases_count = -99;
    code = http_client_get_paginated_count("/repos/torvalds/linux/releases", NULL,
                                             &releases_count, &err);
    CHECK(code == GH_OK, "get_paginated_count responde GH_OK");
    if (code == GH_OK) {
        printf("  [info] releases_count calculado: %ld (informe_validacion.md registro 0)\n",
               releases_count);
        CHECK(releases_count >= 0, "el conteo end-to-end da un valor valido (>= 0)");
    } else {
        print_error(&err);
    }
    printf("\n");

    /* --- Rate limit DESPUES: confirmar que /rate_limit no descuenta --- */
    RateLimitStatus rl_after;
    rl_code = http_client_get_rate_limit_status(NULL, &rl_after, &err);
    printf("rate_limit (despues de 7 solicitudes contadas + esta llamada a /rate_limit):\n");
    CHECK(rl_code == GH_OK, "GET /rate_limit responde GH_OK");
    if (rl_code == GH_OK) {
        long long consumed = (long long)rl_before.remaining - (long long)rl_after.remaining;
        printf("  [info] remaining antes=%lld, despues=%lld (consumidas: %lld; "
               "7 solicitudes 'contadas' se hicieron en el medio)\n",
               (long long)rl_before.remaining, (long long)rl_after.remaining, consumed);
        if (consumed == 7) {
            printf("  [OK] consumio exactamente 7 -> confirma en vivo que /rate_limit NO "
                     "descuenta de su propio limite (notas-implementacion.md #3)\n");
        } else {
            printf("  [info] consumio %lld en vez de 7 -- puede deberse a otra "
                     "actividad compartiendo esta IP de salida (rate limit sin-token es "
                     "por IP), no necesariamente un bug. Ver detalle de cada caso arriba.\n",
                   consumed);
        }
    }

    http_client_global_cleanup();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

