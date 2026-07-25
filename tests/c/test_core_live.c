/*
 * test_core_live.c — Sprint 3.5, smoke test EN VIVO de core_consolidate()
 * (el wrapper real, no la variante inyectable).
 *
 * La logica de secuenciamiento en si (donde corta, la excepcion de
 * contributors) ya esta cubierta de forma determinista y completa en
 * test_core_unit.c, con dependencias inyectadas -ver la nota en core.h
 * sobre por que se opto por eso en vez de seguir insistiendo con contar
 * solicitudes via /rate_limit en esta sandbox en particular (poco
 * confiable, red con multiples IPs de salida).
 *
 * Lo unico que este archivo verifica que el otro no puede: que el
 * WIRING real (core_consolidate() -> http_client_get_and_classify() /
 * http_client_get_paginated_count() reales, no fakes) funciona de
 * punta a punta contra la API real. Un solo repo, chico y conocido
 * (octocat/Hello-World, valores ya confirmados en
 * tests/prototype/informe_validacion.md 3.1), para gastar poco cupo del
 * pool compartido. Se corre con `make test-core-live`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"

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

int main(void) {
    printf("=== Live smoke test: Sprint 3.5 (core_consolidate real, "
           "octocat/Hello-World) ===\n\n");

    if (!http_client_global_init()) {
        fprintf(stderr, "http_client_global_init() fallo, abortando.\n");
        return 1;
    }

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate("octocat", "Hello-World", NULL, &info, &err);

    CHECK(code == GH_OK, "core_consolidate() responde GH_OK contra la API real");
    if (code == GH_OK) {
        CHECK(info->id == 1296269, "id coincide con informe_validacion.md (1296269)");
        CHECK(strcmp(info->name, "Hello-World") == 0, "name correcto");
        CHECK(strcmp(info->owner, "octocat") == 0,
              "owner correcto (extraido de owner.login anidado por json_parser_parse_repo, "
              "via el wiring real)");
        CHECK(info->languages_count == 0,
              "languages vacio (Hello-World no tiene lenguajes detectados, igual que el "
              "ejemplo del README)");
        CHECK(info->contributors_count.present,
              "contributors_count presente (repo chico, no dispara el caso 'too large')");
        CHECK(info->branches_count > 0, "branches_count > 0 (via el truco per_page=1 + "
                                          "header Link real, o el fallback real)");
        printf("  [info] stars=%lld forks=%lld contributors_count=%lld branches_count=%lld "
               "releases_count=%lld\n",
               (long long)info->stars, (long long)info->forks,
               info->contributors_count.present ? (long long)info->contributors_count.value
                                                 : -1,
               (long long)info->branches_count, (long long)info->releases_count);
        repo_info_free(info);
    } else {
        printf("  [info] error: codigo=%d status=%ld msg=\"%s\"\n", err.code, err.status_code,
               err.message);
    }

    http_client_global_cleanup();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

