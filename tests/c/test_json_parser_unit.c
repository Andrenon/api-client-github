/*
 * test_json_parser_unit.c — Sprint 3.3, unit tests offline.
 *
 * Mismo espiritu que test_http_client_unit.c (Sprint 3.2): funciones
 * puras probadas con JSON escrito a mano, sin red. Ac'a es incluso mas
 * simple que en http_client.c: json_parser.c ni siquiera necesita un
 * HttpResponse fabricado, alcanza con un string.
 *
 * Se corre con `make test-unit` (agrupado junto con los de http_client).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_parser.h"
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

/* ------------------------------------------------------------------- */
/* json_parser_parse_repo()                                              */
/* ------------------------------------------------------------------- */

/* JSON real de /repos/octocat/Hello-World, recortado a los campos que
 * consolidate.py efectivamente usa (ver README.md, "Esquema JSON
 * Consolidado", y los valores reales de informe_validacion.md 3.1). */
static const char *HELLO_WORLD_JSON =
    "{"
    "\"id\": 1296269,"
    "\"name\": \"Hello-World\","
    "\"owner\": {\"login\": \"octocat\", \"id\": 583231},"
    "\"description\": \"My first repository on GitHub!\","
    "\"stargazers_count\": 3690,"
    "\"forks_count\": 6261,"
    "\"watchers_count\": 3690,"
    "\"default_branch\": \"master\""
    "}";

static void test_parse_repo_happy_path(void) {
    printf("parse_repo: caso feliz (octocat/Hello-World)\n");
    RepoInfo *info = repo_info_new();
    GitHubError err;

    GitHubErrorCode code = json_parser_parse_repo(HELLO_WORLD_JSON, info, &err);

    CHECK(code == GH_OK, "parsea sin error");
    CHECK(info->id == 1296269, "id correcto");
    CHECK(info->name != NULL && strcmp(info->name, "Hello-World") == 0, "name correcto");
    CHECK(info->owner != NULL && strcmp(info->owner, "octocat") == 0,
          "owner extraido de owner.login (no de un campo 'owner' plano)");
    CHECK(info->description != NULL &&
              strcmp(info->description, "My first repository on GitHub!") == 0,
          "description correcta");
    CHECK(info->stars == 3690, "stars (stargazers_count) correcto");
    CHECK(info->forks == 6261, "forks (forks_count) correcto");
    CHECK(info->watchers == 3690, "watchers (watchers_count) correcto");
    CHECK(info->default_branch != NULL && strcmp(info->default_branch, "master") == 0,
          "default_branch correcto");

    repo_info_free(info);
}

static void test_parse_repo_description_null_explicito(void) {
    printf("parse_repo: description explicitamente en JSON null\n");
    const char *json = "{\"id\":1,\"name\":\"x\",\"owner\":{\"login\":\"o\"},"
                        "\"description\":null,\"stargazers_count\":0,\"forks_count\":0,"
                        "\"watchers_count\":0,\"default_branch\":\"main\"}";
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_repo(json, info, &err);
    CHECK(code == GH_OK, "JSON null en description no es un error");
    CHECK(info->description == NULL,
          "description == NULL (equivalente a repo_data.get('description') == None)");
    repo_info_free(info);
}

static void test_parse_repo_description_ausente(void) {
    printf("parse_repo: description ausente del todo (ni la clave existe)\n");
    const char *json = "{\"id\":1,\"name\":\"x\",\"owner\":{\"login\":\"o\"},"
                        "\"stargazers_count\":0,\"forks_count\":0,"
                        "\"watchers_count\":0,\"default_branch\":\"main\"}";
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_repo(json, info, &err);
    CHECK(code == GH_OK, "clave ausente tampoco es un error");
    CHECK(info->description == NULL, "description == NULL igual que con JSON null explicito");
    repo_info_free(info);
}

static void test_parse_repo_falta_id(void) {
    printf("parse_repo: falta el campo 'id' (requerido)\n");
    const char *json = "{\"name\":\"x\",\"owner\":{\"login\":\"o\"},"
                        "\"stargazers_count\":0,\"forks_count\":0,"
                        "\"watchers_count\":0,\"default_branch\":\"main\"}";
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_repo(json, info, &err);
    CHECK(code == GH_ERR_INTERNAL, "id ausente -> GH_ERR_INTERNAL (no crashea)");
    CHECK(strstr(err.message, "id") != NULL, "el mensaje de error menciona el campo 'id'");
    CHECK(info->name == NULL,
          "info NO quedo modificado a medias (name sigue NULL pese a venir antes en el JSON)");
    repo_info_free(info);
}

static void test_parse_repo_owner_sin_login(void) {
    printf("parse_repo: owner presente pero sin 'login' adentro\n");
    const char *json = "{\"id\":1,\"name\":\"x\",\"owner\":{\"id\":583231},"
                        "\"stargazers_count\":0,\"forks_count\":0,"
                        "\"watchers_count\":0,\"default_branch\":\"main\"}";
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_repo(json, info, &err);
    CHECK(code == GH_ERR_INTERNAL, "owner sin login -> GH_ERR_INTERNAL");
    CHECK(strstr(err.message, "owner.login") != NULL,
          "el mensaje distingue 'owner.login' del resto de los campos");
    repo_info_free(info);
}

static void test_parse_repo_tipo_incorrecto(void) {
    printf("parse_repo: stargazers_count con tipo incorrecto (string en vez de number)\n");
    const char *json = "{\"id\":1,\"name\":\"x\",\"owner\":{\"login\":\"o\"},"
                        "\"stargazers_count\":\"muchas\",\"forks_count\":0,"
                        "\"watchers_count\":0,\"default_branch\":\"main\"}";
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_repo(json, info, &err);
    CHECK(code == GH_ERR_INTERNAL, "tipo incorrecto -> GH_ERR_INTERNAL, no crashea");
    repo_info_free(info);
}

static void test_parse_repo_body_no_json(void) {
    printf("parse_repo: body que no es JSON valido\n");
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_repo("esto no es json", info, &err);
    CHECK(code == GH_ERR_INTERNAL, "body invalido -> GH_ERR_INTERNAL, no crashea");
    repo_info_free(info);
}

static void test_parse_repo_reemplaza_valores_previos(void) {
    printf("parse_repo: llamado dos veces sobre el mismo RepoInfo no pierde memoria\n");
    RepoInfo *info = repo_info_new();
    GitHubError err;
    json_parser_parse_repo(HELLO_WORLD_JSON, info, &err);

    const char *json2 = "{\"id\":2,\"name\":\"otro\",\"owner\":{\"login\":\"otro-owner\"},"
                         "\"stargazers_count\":1,\"forks_count\":1,"
                         "\"watchers_count\":1,\"default_branch\":\"main\"}";
    GitHubErrorCode code = json_parser_parse_repo(json2, info, &err);
    CHECK(code == GH_OK, "segunda llamada exitosa");
    CHECK(strcmp(info->name, "otro") == 0,
          "el segundo parseo reemplaza el name anterior (libera el viejo antes)");
    CHECK(info->description == NULL,
          "el segundo JSON no tiene description -> pisa la anterior con NULL");
    repo_info_free(info);
}

/* ------------------------------------------------------------------- */
/* json_parser_parse_languages()                                         */
/* ------------------------------------------------------------------- */

static void test_parse_languages_happy_path(void) {
    printf("parse_languages: caso feliz (torvalds/linux, ver README ejemplo)\n");
    const char *json = "{\"C\": 950000000, \"Assembly\": 12000000}";
    RepoInfo *info = repo_info_new();
    GitHubError err;

    GitHubErrorCode code = json_parser_parse_languages(json, info, &err);

    CHECK(code == GH_OK, "parsea sin error");
    CHECK(info->languages_count == 2, "languages_count == 2");
    if (info->languages_count == 2) {
        CHECK(strcmp(info->languages[0].name, "C") == 0 && info->languages[0].bytes == 950000000,
              "primer lenguaje (C) correcto, orden preservado");
        CHECK(strcmp(info->languages[1].name, "Assembly") == 0 &&
                  info->languages[1].bytes == 12000000,
              "segundo lenguaje (Assembly) correcto");
    }
    repo_info_free(info);
}

static void test_parse_languages_vacio(void) {
    printf("parse_languages: objeto vacio '{}' (caso real: octocat/Hello-World)\n");
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_languages("{}", info, &err);
    CHECK(code == GH_OK, "objeto vacio NO es un error");
    CHECK(info->languages_count == 0, "languages_count == 0");
    CHECK(info->languages == NULL, "languages queda en NULL (no un array de largo 0 reservado)");
    repo_info_free(info);
}

static void test_parse_languages_no_es_objeto(void) {
    printf("parse_languages: body es un array en vez de un objeto\n");
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code = json_parser_parse_languages("[1,2,3]", info, &err);
    CHECK(code == GH_ERR_INTERNAL, "array en vez de objeto -> GH_ERR_INTERNAL");
    repo_info_free(info);
}

static void test_parse_languages_valor_no_numerico(void) {
    printf("parse_languages: un lenguaje con valor no numerico\n");
    RepoInfo *info = repo_info_new();
    GitHubError err;
    GitHubErrorCode code =
        json_parser_parse_languages("{\"C\": 100, \"Rust\": \"muchos\"}", info, &err);
    CHECK(code == GH_ERR_INTERNAL, "valor no numerico -> GH_ERR_INTERNAL, no guarda basura");
    CHECK(strstr(err.message, "Rust") != NULL, "el mensaje identifica cual lenguaje fallo");
    CHECK(info->languages_count == 0, "no queda un array a medio poblar tras el error");
    repo_info_free(info);
}

/* ------------------------------------------------------------------- */

int main(void) {
    printf("=== Unit tests: Sprint 3.3 (json_parser, offline) ===\n\n");

    test_parse_repo_happy_path();
    test_parse_repo_description_null_explicito();
    test_parse_repo_description_ausente();
    test_parse_repo_falta_id();
    test_parse_repo_owner_sin_login();
    test_parse_repo_tipo_incorrecto();
    test_parse_repo_body_no_json();
    test_parse_repo_reemplaza_valores_previos();
    printf("\n");
    test_parse_languages_happy_path();
    test_parse_languages_vacio();
    test_parse_languages_no_es_objeto();
    test_parse_languages_valor_no_numerico();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

