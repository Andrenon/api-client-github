/*
 * smoke_test.c — Sprint 3.1 (setup del proyecto en C)
 *
 * No prueba lógica de negocio (no existe todavía: arranca en el Sprint
 * 3.2 con http_client.c). Su único objetivo es confirmar, antes de seguir
 * escribiendo código sobre esta base, que:
 *
 *   1. libcurl, sqlite3 y cJSON están instalados, sus headers se
 *      encuentran y el binario linkea contra las tres.
 *   2. include/models.h compila limpio con -Wall -Wextra -Wpedantic.
 *   3. El ciclo de vida de RepoInfo (repo_info_new -> poblar ->
 *      repo_info_free) no pierde memoria ni revienta, incluyendo el caso
 *      description == NULL y el array de languages.
 *
 * Se corre con `make smoke-test`.
 */

#include <curl/curl.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>

#include "models.h"

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

static void check_libcurl(void) {
    printf("libcurl:\n");
    CURLcode init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    CHECK(init_result == CURLE_OK, "curl_global_init() devuelve CURLE_OK");

    curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    CHECK(info != NULL, "curl_version_info() no es NULL");
    if (info != NULL) {
        printf("       version: %s (SSL: %s)\n", info->version,
               info->ssl_version ? info->ssl_version : "ninguno");
        CHECK(info->features & CURL_VERSION_SSL,
              "libcurl fue compilado con soporte SSL/TLS (necesario para HTTPS)");
    }

    curl_global_cleanup();
}

static void check_sqlite3(void) {
    printf("sqlite3:\n");
    printf("       version: %s\n", sqlite3_libversion());

    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    CHECK(rc == SQLITE_OK, "sqlite3_open(\":memory:\") devuelve SQLITE_OK");

    rc = sqlite3_exec(db, "CREATE TABLE t (x INTEGER);", NULL, NULL, NULL);
    CHECK(rc == SQLITE_OK, "CREATE TABLE de prueba se ejecuta sin error");

    sqlite3_close(db);
}

static void check_cjson(void) {
    printf("cJSON:\n");
    printf("       version: %s\n", cJSON_Version());

    const char *raw = "{\"name\":\"linux\",\"stargazers_count\":215000}";
    cJSON *parsed = cJSON_Parse(raw);
    CHECK(parsed != NULL, "cJSON_Parse() parsea un JSON de prueba");

    if (parsed != NULL) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(parsed, "name");
        CHECK(cJSON_IsString(name) && strcmp(name->valuestring, "linux") == 0,
              "extrae el campo string 'name' correctamente");

        cJSON *stars = cJSON_GetObjectItemCaseSensitive(parsed, "stargazers_count");
        CHECK(cJSON_IsNumber(stars) && stars->valueint == 215000,
              "extrae el campo numerico 'stargazers_count' correctamente");
    }
    cJSON_Delete(parsed);
}

static void check_models(void) {
    printf("models.h (RepoInfo):\n");

    RepoInfo *info = repo_info_new();
    CHECK(info != NULL, "repo_info_new() no devuelve NULL");
    CHECK(info->name == NULL && info->description == NULL &&
              info->languages == NULL && info->languages_count == 0 &&
              info->contributors_count.present == false && info->id == 0,
          "estado inicial: punteros en NULL, contadores en 0, "
          "contributors_count ausente");

    /* Simula lo que hara json_parser.c en el Sprint 3.3: poblar los
     * campos owned a mano. */
    info->id = 2325298;
    info->name = strdup("linux");
    info->owner = strdup("torvalds");
    info->description = NULL; /* caso real: torvalds/linux SI tiene
                                  descripcion, pero se prueba el caso NULL
                                  a proposito porque es el que puede
                                  romper repo_info_free() si no se maneja
                                  bien */
    info->default_branch = strdup("master");
    info->stars = 215000;
    info->contributors_count.present = false; /* caso "too large to list" */

    bool langs_ok = repo_info_set_languages(info, 2);
    CHECK(langs_ok, "repo_info_set_languages(info, 2) reserva el array");
    if (langs_ok) {
        info->languages[0].name = strdup("C");
        info->languages[0].bytes = 950000000;
        info->languages[1].name = strdup("Assembly");
        info->languages[1].bytes = 12000000;
    }

    CHECK(info->name != NULL && strcmp(info->name, "linux") == 0,
          "campo name poblado correctamente");
    CHECK(info->description == NULL,
          "description == NULL se preserva (no revienta free() despues)");
    CHECK(info->languages_count == 2 &&
              strcmp(info->languages[0].name, "C") == 0,
          "array de languages poblado correctamente");

    /* Si esto corre bien bajo valgrind (ver salida de make smoke-test /
     * valgrind), confirma que no hay leaks ni accesos invalidos al
     * liberar un RepoInfo con description NULL + languages poblado. */
    repo_info_free(info);
    printf("  [OK] repo_info_free() libera sin crashear (name, owner, "
           "default_branch, languages[].name, description=NULL, array)\n");
}

int main(void) {
    printf("=== Smoke test: Sprint 3.1 (setup del proyecto en C) ===\n\n");

    check_libcurl();
    printf("\n");
    check_sqlite3();
    printf("\n");
    check_cjson();
    printf("\n");
    check_models();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS",
           failures);
    return failures == 0 ? 0 : 1;
}
