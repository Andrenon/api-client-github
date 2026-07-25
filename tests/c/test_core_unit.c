/*
 * test_core_unit.c — Sprint 3.5, unit tests offline con inyeccion de
 * dependencias.
 *
 * core_consolidate_with() recibe las dos funciones de transporte como
 * parametros (ver core.h). Ac'a se inyectan versiones falsas que
 * devuelven resultados prefijados segun el orden de llamada -sin tocar
 * la red-, y se cuenta cuantas veces se llamo cada una para verificar
 * exactamente donde corta la secuencia. Es el reemplazo determinista del
 * intento fallido de contar solicitudes via /rate_limit en vivo (ver
 * docs/comandos-manuales.md, nota del Sprint 3.5): mismo objetivo,
 * sin depender de una red compartida y ruidosa.
 *
 * Se corre con `make test-unit`.
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

/* ------------------------------------------------------------------- */
/* Fakes: 2 llamadas via get_and_classify (repo, languages) + 3 via     */
/* get_paginated_count (contributors, releases, branches), en ese orden. */
/* ------------------------------------------------------------------- */

#define N_CLASSIFY_STEPS 2
#define N_COUNT_STEPS 3

static int g_classify_calls;
static GitHubErrorCode g_classify_results[N_CLASSIFY_STEPS];
static const char *g_classify_bodies[N_CLASSIFY_STEPS];

static int g_count_calls;
static GitHubErrorCode g_count_results[N_COUNT_STEPS];
static long g_count_values[N_COUNT_STEPS];

static void reset_fakes(void) {
    g_classify_calls = 0;
    g_count_calls = 0;
    for (int i = 0; i < N_CLASSIFY_STEPS; i++) {
        g_classify_results[i] = GH_OK;
        g_classify_bodies[i] = "{}";
    }
    for (int i = 0; i < N_COUNT_STEPS; i++) {
        g_count_results[i] = GH_OK;
        g_count_values[i] = 0;
    }
}

static GitHubErrorCode fake_get_and_classify(const char *path, const char *token,
                                              const char *query, HttpResponse **out_response,
                                              GitHubError *out_error) {
    (void)token;
    (void)query;
    if (g_classify_calls >= N_CLASSIFY_STEPS) {
        fprintf(stderr, "fake_get_and_classify: llamada de mas (path=%s)\n", path);
        exit(1);
    }
    int step = g_classify_calls++;

    GitHubErrorCode result = g_classify_results[step];
    if (result != GH_OK) {
        *out_response = NULL;
        github_error_set(out_error, result, 0, "fake_get_and_classify error");
        return result;
    }

    HttpResponse *response = calloc(1, sizeof(HttpResponse));
    response->status_code = 200;
    response->body = strdup(g_classify_bodies[step]);
    response->body_len = strlen(response->body);
    response->headers = NULL;
    response->headers_count = 0;
    *out_response = response;
    return GH_OK;
}

static GitHubErrorCode fake_get_paginated_count(const char *path, const char *token,
                                                 long *out_count, GitHubError *out_error) {
    (void)token;
    if (g_count_calls >= N_COUNT_STEPS) {
        fprintf(stderr, "fake_get_paginated_count: llamada de mas (path=%s)\n", path);
        exit(1);
    }
    int step = g_count_calls++;

    GitHubErrorCode result = g_count_results[step];
    if (result == GH_OK) {
        *out_count = g_count_values[step];
    } else {
        github_error_set(out_error, result, 0, "fake_get_paginated_count error");
    }
    return result;
}

/* JSON minimo pero valido para los pasos 1 y 2 (repo, languages), en el
 * shape crudo que espera json_parser_parse_repo/parse_languages -no el
 * consolidado-. */
static const char *SAMPLE_REPO_JSON =
    "{\"id\":42,\"name\":\"demo\",\"owner\":{\"login\":\"demo-owner\"},"
    "\"description\":\"repo de prueba\",\"stargazers_count\":10,\"forks_count\":2,"
    "\"watchers_count\":10,\"default_branch\":\"main\"}";
static const char *SAMPLE_LANGUAGES_JSON = "{\"C\":100}";

/* ------------------------------------------------------------------- */

static void test_secuencia_completa_exitosa(void) {
    printf("core_consolidate_with: secuencia completa exitosa (5/5 llamadas)\n");
    reset_fakes();
    g_classify_bodies[0] = SAMPLE_REPO_JSON;
    g_classify_bodies[1] = SAMPLE_LANGUAGES_JSON;
    g_count_values[0] = 7;  /* contributors */
    g_count_values[1] = 3;  /* releases */
    g_count_values[2] = 12; /* branches */

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate_with(
        "demo-owner", "demo", NULL, fake_get_and_classify, fake_get_paginated_count, &info, &err);

    CHECK(code == GH_OK, "devuelve GH_OK");
    CHECK(g_classify_calls == 2 && g_count_calls == 3, "se hicieron las 5 llamadas, ninguna de mas");
    if (info != NULL) {
        CHECK(info->id == 42 && strcmp(info->name, "demo") == 0, "campos de repo poblados");
        CHECK(info->languages_count == 1, "languages poblado");
        CHECK(info->contributors_count.present && info->contributors_count.value == 7,
              "contributors_count poblado y presente");
        CHECK(info->releases_count == 3, "releases_count poblado");
        CHECK(info->branches_count == 12, "branches_count poblado");
        repo_info_free(info);
    }
}

static void test_corta_en_paso_1(void) {
    printf("core_consolidate_with: falla el paso 1 (repo) -> corta ahi, 0 llamadas mas\n");
    reset_fakes();
    g_classify_results[0] = GH_ERR_NOT_FOUND;

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate_with(
        "x", "y", NULL, fake_get_and_classify, fake_get_paginated_count, &info, &err);

    CHECK(code == GH_ERR_NOT_FOUND, "propaga GH_ERR_NOT_FOUND");
    CHECK(info == NULL, "*out_info queda NULL, no hay objeto parcial");
    CHECK(g_classify_calls == 1, "solo se llamo 1 vez a get_and_classify (no llego a "
                                  "languages)");
    CHECK(g_count_calls == 0, "get_paginated_count no se llamo ni una vez");
}

static void test_corta_en_paso_2(void) {
    printf("core_consolidate_with: falla el paso 2 (languages) -> corta ahi, contributors/"
           "releases/branches no se llaman\n");
    reset_fakes();
    g_classify_bodies[0] = SAMPLE_REPO_JSON;
    g_classify_results[1] = GH_ERR_RATE_LIMIT_EXCEEDED;

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate_with(
        "x", "y", NULL, fake_get_and_classify, fake_get_paginated_count, &info, &err);

    CHECK(code == GH_ERR_RATE_LIMIT_EXCEEDED, "propaga el error de languages");
    CHECK(info == NULL, "*out_info queda NULL");
    CHECK(g_classify_calls == 2, "se llamo a repo Y a languages (fallo en el segundo)");
    CHECK(g_count_calls == 0, "nunca se llego a contributors/releases/branches");
}

static void test_contributors_too_large_no_corta(void) {
    printf("core_consolidate_with: contributors 'too large' NO corta la secuencia (caso "
           "real torvalds/linux, ver notas-implementacion.md #1)\n");
    reset_fakes();
    g_classify_bodies[0] = SAMPLE_REPO_JSON;
    g_classify_bodies[1] = SAMPLE_LANGUAGES_JSON;
    g_count_results[0] = GH_ERR_RESOURCE_TOO_LARGE; /* contributors */
    g_count_values[1] = 0;                          /* releases */
    g_count_values[2] = 1;                           /* branches */

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate_with(
        "torvalds", "linux", NULL, fake_get_and_classify, fake_get_paginated_count, &info, &err);

    CHECK(code == GH_OK, "el resultado GENERAL es GH_OK pese al error puntual en "
                          "contributors");
    CHECK(g_count_calls == 3, "SI se llego a llamar releases y branches (no se corto en "
                               "contributors)");
    if (info != NULL) {
        CHECK(!info->contributors_count.present,
              "contributors_count.present == false (la unica consecuencia del 'too "
              "large')");
        CHECK(info->releases_count == 0 && info->branches_count == 1,
              "releases_count y branches_count SI se calcularon con normalidad");
        repo_info_free(info);
    }
}

static void test_contributors_error_generico_si_corta(void) {
    printf("core_consolidate_with: un error DISTINTO de 'too large' en contributors SI "
           "corta (ej. 401)\n");
    reset_fakes();
    g_classify_bodies[0] = SAMPLE_REPO_JSON;
    g_classify_bodies[1] = SAMPLE_LANGUAGES_JSON;
    g_count_results[0] = GH_ERR_INVALID_TOKEN; /* NO es GH_ERR_RESOURCE_TOO_LARGE */

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate_with(
        "x", "y", NULL, fake_get_and_classify, fake_get_paginated_count, &info, &err);

    CHECK(code == GH_ERR_INVALID_TOKEN, "propaga el error real de contributors (no lo "
                                         "confunde con el caso especial)");
    CHECK(info == NULL, "*out_info queda NULL");
    CHECK(g_count_calls == 1, "corto en contributors: releases y branches NO se llamaron");
}

static void test_corta_en_releases(void) {
    printf("core_consolidate_with: falla releases -> corta antes de branches\n");
    reset_fakes();
    g_classify_bodies[0] = SAMPLE_REPO_JSON;
    g_classify_bodies[1] = SAMPLE_LANGUAGES_JSON;
    g_count_values[0] = 5;                    /* contributors: ok */
    g_count_results[1] = GH_ERR_UNEXPECTED_STATUS; /* releases: falla */

    RepoInfo *info = NULL;
    GitHubError err;
    GitHubErrorCode code = core_consolidate_with(
        "x", "y", NULL, fake_get_and_classify, fake_get_paginated_count, &info, &err);

    CHECK(code == GH_ERR_UNEXPECTED_STATUS, "propaga el error de releases");
    CHECK(info == NULL, "*out_info queda NULL");
    CHECK(g_count_calls == 2, "se llamo a contributors y releases; branches NUNCA se llamo");
}

/* ------------------------------------------------------------------- */

int main(void) {
    printf("=== Unit tests: Sprint 3.5 (core, offline con dependencias inyectadas) ===\n\n");

    test_secuencia_completa_exitosa();
    printf("\n");
    test_corta_en_paso_1();
    printf("\n");
    test_corta_en_paso_2();
    printf("\n");
    test_contributors_too_large_no_corta();
    printf("\n");
    test_contributors_error_generico_si_corta();
    printf("\n");
    test_corta_en_releases();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

