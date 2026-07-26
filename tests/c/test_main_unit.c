/*
 * test_main_unit.c — Sprint 3.6, unit tests offline de main.c.
 *
 * parse_args() y format_reset() son funciones `static` de main.c (no
 * tiene un header propio: es el punto de entrada, no una libreria que
 * otros .c importen). Para poder probarlas sin agrandar la superficie
 * publica de main.c solo para tests, se usa un truco liviano y comun en
 * C: incluir main.c directamente como fuente, renombrando su `main`
 * real via macro para que no choque con el `main` de este archivo de
 * test. El preprocesador solo reemplaza el TOKEN `main`, no toca
 * strings ni comentarios, asi que es seguro.
 *
 * Las funciones de impresion (print_human_summary, print_core_error)
 * NO se prueban ac'a de forma automatizada -se validaron a ojo contra
 * corridas reales del binario (ver docs/comandos-manuales.md, Sprint
 * 3.6): son formato de texto de bajo riesgo sobre datos ya validados en
 * otros lados. Un chequeo mas exhaustivo, si hace falta, encaja mejor en
 * la Fase 4 (Testing e integracion, ver docs/workplan.md) que en este
 * sprint.
 *
 * Se corre con `make test-unit`.
 */

#define main main_unused_entry_point
#include "../../src/main.c"
#undef main

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
/* parse_args()                                                          */
/* ------------------------------------------------------------------- */

static void test_parse_args_valido_sin_json(void) {
    printf("parse_args: \"torvalds/linux\" (sin --json)\n");
    char *argv[] = {"github-client", "torvalds/linux"};
    CliArgs args;
    bool ok = parse_args(2, argv, &args);
    CHECK(ok, "parsea correctamente");
    CHECK(strcmp(args.owner, "torvalds") == 0, "owner == 'torvalds'");
    CHECK(strcmp(args.repo, "linux") == 0, "repo == 'linux'");
    CHECK(!args.as_json, "as_json == false por defecto");
    cli_args_free(&args);
}

static void test_parse_args_valido_con_json(void) {
    printf("parse_args: \"torvalds/linux\" --json\n");
    char *argv[] = {"github-client", "torvalds/linux", "--json"};
    CliArgs args;
    bool ok = parse_args(3, argv, &args);
    CHECK(ok, "parsea correctamente");
    CHECK(args.as_json, "as_json == true");
    cli_args_free(&args);
}

static void test_parse_args_sin_argumentos(void) {
    printf("parse_args: sin argumentos\n");
    char *argv[] = {"github-client"};
    CliArgs args;
    bool ok = parse_args(1, argv, &args);
    CHECK(!ok, "rechaza sin argumentos");
}

static void test_parse_args_demasiados_argumentos(void) {
    printf("parse_args: 3 argumentos ademas del programa (de mas)\n");
    char *argv[] = {"github-client", "torvalds/linux", "--json", "algo-mas"};
    CliArgs args;
    bool ok = parse_args(4, argv, &args);
    CHECK(!ok, "rechaza con mas de 2 argumentos");
}

static void test_parse_args_sin_slash(void) {
    printf("parse_args: \"torvalds-linux\" (sin '/')\n");
    char *argv[] = {"github-client", "torvalds-linux"};
    CliArgs args;
    bool ok = parse_args(2, argv, &args);
    CHECK(!ok, "rechaza sin '/'");
}

static void test_parse_args_dos_slashes(void) {
    printf("parse_args: \"a/b/c\" (dos '/', de mas)\n");
    char *argv[] = {"github-client", "a/b/c"};
    CliArgs args;
    bool ok = parse_args(2, argv, &args);
    CHECK(!ok, "rechaza con mas de un '/'");
}

static void test_parse_args_owner_vacio(void) {
    printf("parse_args: \"/linux\" (owner vacio)\n");
    char *argv[] = {"github-client", "/linux"};
    CliArgs args;
    bool ok = parse_args(2, argv, &args);
    CHECK(!ok, "rechaza owner vacio");
}

static void test_parse_args_repo_vacio(void) {
    printf("parse_args: \"torvalds/\" (repo vacio)\n");
    char *argv[] = {"github-client", "torvalds/"};
    CliArgs args;
    bool ok = parse_args(2, argv, &args);
    CHECK(!ok, "rechaza repo vacio");
}

static void test_parse_args_opcion_desconocida(void) {
    printf("parse_args: \"torvalds/linux\" --xml (opcion invalida)\n");
    char *argv[] = {"github-client", "torvalds/linux", "--xml"};
    CliArgs args;
    bool ok = parse_args(3, argv, &args);
    CHECK(!ok, "rechaza una segunda opcion que no sea --json");
}

/* ------------------------------------------------------------------- */
/* format_reset()                                                        */
/* ------------------------------------------------------------------- */

static void test_format_reset_sin_valor(void) {
    printf("format_reset: has_reset_at=false\n");
    char buf[16];
    bool ok = format_reset(false, 0, buf, sizeof(buf));
    CHECK(!ok, "devuelve false cuando no hay reset_at disponible");
}

static void test_format_reset_valor_conocido(void) {
    printf("format_reset: epoch conocido -> HH:MM:SS en hora local\n");
    /* 1700000000 (epoch UNIX) = 2023-11-14T22:13:20Z. En un contenedor
     * con TZ=UTC (confirmado con `date` vs `date -u` durante las
     * pruebas manuales de este sprint) el resultado esperado es
     * "22:13:20". Si este test corriera con otro TZ, fallaria -es un
     * trade-off aceptado a cambio de verificar el valor exacto en vez
     * de solo "no crashea". */
    char buf[16];
    bool ok = format_reset(true, 1700000000, buf, sizeof(buf));
    CHECK(ok, "devuelve true cuando hay reset_at");
    CHECK(strcmp(buf, "22:13:20") == 0, "formatea 1700000000 como 22:13:20 (TZ=UTC)");
}

static void test_format_reset_buffer_chico(void) {
    printf("format_reset: buffer de salida mas chico que \"HH:MM:SS\"\n");
    char buf[4]; /* insuficiente para "HH:MM:SS\0" (9 bytes) */
    bool ok = format_reset(true, 1700000000, buf, sizeof(buf));
    CHECK(!ok, "strftime devuelve 0 si el buffer no alcanza; format_reset lo propaga como "
               "false en vez de dejar un string truncado silenciosamente");
}

/* ------------------------------------------------------------------- */

/* ------------------------------------------------------------------- */
/* Camino exitoso completo, SIN red: construye un RepoInfo a mano (como  */
/* si core_consolidate ya hubiera terminado) y ejercita EXACTAMENTE los  */
/* mismos pasos que el bloque de exito de main() -serializar compacto,   */
/* abrir/persistir en SQLite, serializar pretty, imprimir el resumen     */
/* humano-. Esto cubre lo que las corridas en vivo contra la API real no */
/* pudieron confirmar en esta sandbox (ver docs/comandos-manuales.md,    */
/* Sprint 3.6): que el WIRING de main.c despues de un core_consolidate   */
/* exitoso funciona de punta a punta, sin depender de la red para        */
/* probarlo -la parte de red ya esta cubierta por separado en            */
/* test_core_unit.c (secuenciamiento) y por las corridas manuales del    */
/* binario real que SI llegaron a pegarle a la API (ver la nota en el    */
/* archivo de comandos-manuales.md).                                     */
/* ------------------------------------------------------------------- */

static RepoInfo *build_sample_repo_info(void) {
    RepoInfo *info = repo_info_new();
    info->id = 1296269;
    info->name = strdup("Hello-World");
    info->owner = strdup("octocat");
    info->description = strdup("My first repository on GitHub!");
    info->stars = 3690;
    info->forks = 6261;
    info->watchers = 3690;
    info->default_branch = strdup("master");
    repo_info_set_languages(info, 2);
    info->languages[0].name = strdup("Ruby");
    info->languages[0].bytes = 100;
    info->languages[1].name = strdup("JavaScript");
    info->languages[1].bytes = 50;
    info->contributors_count.present = true;
    info->contributors_count.value = 3;
    info->branches_count = 3;
    info->releases_count = 0;
    return info;
}

/* dup/dup2 en vez de un segundo freopen "de vuelta": freopen no tiene
 * forma portable de "deshacerse" al FILE* original (no hay tal cosa
 * como el path original de stdout si es una pipe/terminal). dup()
 * guarda una copia del file descriptor real ANTES de redirigir, y
 * dup2() lo restaura despues -tecnica POSIX estandar para capturar
 * stdout en un test C. */
static char *capture_stdout_of_print_human_summary(const RepoInfo *info) {
    const char *tmp_path = "/tmp/test_main_unit_stdout.txt";
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    FILE *redirected = freopen(tmp_path, "w", stdout);
    if (redirected == NULL) {
        close(saved_fd);
        return NULL;
    }

    print_human_summary(info);

    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    FILE *f = fopen(tmp_path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    remove(tmp_path);
    return buf;
}

static void test_camino_exitoso_completo_sin_red(void) {
    printf("camino exitoso completo (serializar + persistir + imprimir), sin red, con "
           "un RepoInfo armado a mano equivalente a octocat/Hello-World\n");

    RepoInfo *info = build_sample_repo_info();
    GitHubError err;

    /* 1. Serializar compacto (lo que se guarda en meta_payload) */
    char *meta_json = NULL;
    bool ok = json_parser_serialize_repo_info(info, false, &meta_json, &err);
    CHECK(ok, "serializa el JSON compacto para meta_payload");
    CHECK(ok && strchr(meta_json, '\n') == NULL, "el compacto es una sola linea");

    /* 2. Persistir (mismo llamado exacto que hace main(): title =
     * info->name, no un valor separado) */
    sqlite3 *conn = NULL;
    ok = db_open(":memory:", &conn, &err);
    CHECK(ok, "db_open(:memory:) exitoso");

    ok = db_upsert_asset(conn, info->owner, info->name, info->name, meta_json, &err);
    CHECK(ok, "db_upsert_asset con los mismos argumentos que usa main()");

    DbAssetRow row;
    db_get_asset(conn, info->owner, info->name, &row, &err);
    CHECK(row.found, "el registro persistido se puede recuperar");
    CHECK(row.title != NULL && strcmp(row.title, "Hello-World") == 0,
          "title == info->name (Hello-World), como pasa main()");
    CHECK(row.meta_payload != NULL && strstr(row.meta_payload, "\"stars\":3690") != NULL,
          "meta_payload persistido contiene el JSON serializado real");
    db_asset_row_free(&row);
    db_close(conn);

    /* 3. Serializar pretty (lo que se imprime con --json) */
    char *pretty_json = NULL;
    ok = json_parser_serialize_repo_info(info, true, &pretty_json, &err);
    CHECK(ok, "serializa el JSON pretty para --json");
    CHECK(ok && strchr(pretty_json, '\n') != NULL, "el pretty tiene saltos de linea");

    /* 4. Resumen humano (formato por defecto, sin --json) */
    char *captured = capture_stdout_of_print_human_summary(info);
    CHECK(captured != NULL, "se pudo capturar la salida de print_human_summary");
    if (captured != NULL) {
        CHECK(strstr(captured, "Hello-World (octocat)") != NULL,
              "primera linea: \"{name} ({owner})\"");
        CHECK(strstr(captured, "My first repository on GitHub!") != NULL,
              "incluye la descripcion");
        CHECK(strstr(captured, "stars: 3690") != NULL &&
                  strstr(captured, "forks: 6261") != NULL &&
                  strstr(captured, "watchers: 3690") != NULL,
              "incluye stars/forks/watchers");
        CHECK(strstr(captured, "Ruby, JavaScript") != NULL,
              "lenguajes separados por coma, en el orden del array");
        CHECK(strstr(captured, "contributors: 3") != NULL &&
                  strstr(captured, "branches: 3") != NULL &&
                  strstr(captured, "releases: 0") != NULL,
              "incluye contributors/branches/releases (contributors presente, no N/D)");
        free(captured);
    }

    free(pretty_json);
    free(meta_json);
    repo_info_free(info);
}

static void test_resumen_humano_contributors_ausente(void) {
    printf("print_human_summary: contributors_count ausente -> mensaje N/D (caso real "
           "torvalds/linux)\n");
    RepoInfo *info = build_sample_repo_info();
    info->contributors_count.present = false;

    char *captured = capture_stdout_of_print_human_summary(info);
    CHECK(captured != NULL &&
              strstr(captured, "N/D (repo con demasiado historial") != NULL,
          "muestra el mensaje N/D en vez de un numero cuando contributors_count esta "
          "ausente");
    free(captured);
    repo_info_free(info);
}

static void test_resumen_humano_sin_descripcion(void) {
    printf("print_human_summary: description NULL -> '-'\n");
    RepoInfo *info = build_sample_repo_info();
    free(info->description);
    info->description = NULL;

    char *captured = capture_stdout_of_print_human_summary(info);
    CHECK(captured != NULL && strstr(captured, "descripción: -") != NULL,
          "muestra '-' en vez de un description NULL o vacio");
    free(captured);
    repo_info_free(info);
}

static void test_resumen_humano_sin_lenguajes(void) {
    printf("print_human_summary: languages vacio -> '-' (caso real octocat/Hello-World)\n");
    RepoInfo *info = build_sample_repo_info();
    repo_info_set_languages(info, 0);

    char *captured = capture_stdout_of_print_human_summary(info);
    CHECK(captured != NULL && strstr(captured, "lenguajes: -") != NULL,
          "muestra '-' en vez de una lista vacia");
    free(captured);
    repo_info_free(info);
}

/* ------------------------------------------------------------------- */

int main(void) {
    printf("=== Unit tests: Sprint 3.6 (main.c: parse_args + format_reset) ===\n\n");

    test_parse_args_valido_sin_json();
    test_parse_args_valido_con_json();
    test_parse_args_sin_argumentos();
    test_parse_args_demasiados_argumentos();
    test_parse_args_sin_slash();
    test_parse_args_dos_slashes();
    test_parse_args_owner_vacio();
    test_parse_args_repo_vacio();
    test_parse_args_opcion_desconocida();
    printf("\n");
    test_format_reset_sin_valor();
    test_format_reset_valor_conocido();
    test_format_reset_buffer_chico();
    printf("\n");
    test_camino_exitoso_completo_sin_red();
    printf("\n");
    test_resumen_humano_contributors_ausente();
    test_resumen_humano_sin_descripcion();
    test_resumen_humano_sin_lenguajes();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

