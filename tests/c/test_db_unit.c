/*
 * test_db_unit.c — Sprint 3.4, unit tests offline.
 *
 * Usa db_open(":memory:", ...): SQLite en memoria, sin tocar disco, sin
 * archivos que limpiar entre corridas. Cada test abre su propia
 * conexion en memoria (aislado del resto), asi el orden de ejecucion no
 * importa. Se corre con `make test-unit`.
 */

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"

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

static sqlite3 *open_memory_db(void) {
    sqlite3 *conn = NULL;
    GitHubError err;
    if (!db_open(":memory:", &conn, &err)) {
        fprintf(stderr, "db_open(:memory:) fallo inesperadamente: %s\n", err.message);
        exit(1);
    }
    return conn;
}

static int count_rows(sqlite3 *conn) {
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(conn, "SELECT COUNT(*) FROM assets;", -1, &stmt, NULL);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

/* ------------------------------------------------------------------- */

static void test_open_and_schema(void) {
    printf("db_open: crea el esquema 'assets' (chequeado insertando)\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;
    bool ok = db_upsert_asset(conn, "octocat", "Hello-World", "Hello-World", "{}", &err);
    CHECK(ok, "insertar contra el esquema recien creado funciona sin error");
    db_close(conn);
}

static void test_upsert_insert_nuevo(void) {
    printf("db_upsert_asset: insert de un registro nuevo\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;

    bool ok = db_upsert_asset(conn, "octocat", "Hello-World", "Hello-World",
                               "{\"name\":\"Hello-World\",\"stars\":3690}", &err);
    CHECK(ok, "upsert (insert) exitoso");

    DbAssetRow row;
    ok = db_get_asset(conn, "octocat", "Hello-World", &row, &err);
    CHECK(ok, "get_asset no falla");
    CHECK(row.found, "el registro recien insertado se encuentra");
    CHECK(row.asset_uri != NULL && strcmp(row.asset_uri, "github://octocat/Hello-World") == 0,
          "asset_uri = github://{owner}/{repo}");
    CHECK(row.title != NULL && strcmp(row.title, "Hello-World") == 0, "title correcto");
    CHECK(row.entity != NULL && strcmp(row.entity, "repository") == 0,
          "entity fijo en 'repository'");
    CHECK(row.provider != NULL && strcmp(row.provider, "github") == 0,
          "provider fijo en 'github'");
    CHECK(row.meta_payload != NULL && strstr(row.meta_payload, "3690") != NULL,
          "meta_payload contiene el JSON pasado tal cual");
    CHECK(row.created_at != NULL && row.updated_at != NULL &&
              strcmp(row.created_at, row.updated_at) == 0,
          "en el insert inicial, created_at == updated_at (ambos via DEFAULT "
          "CURRENT_TIMESTAMP)");

    db_asset_row_free(&row);
    db_close(conn);
}

static void test_get_asset_inexistente(void) {
    printf("db_get_asset: repo que no fue insertado -> found=false, no es un error\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;

    DbAssetRow row;
    bool ok = db_get_asset(conn, "nadie", "nada", &row, &err);
    CHECK(ok, "no encontrar el registro NO devuelve false (no es un error de SQLite)");
    CHECK(!row.found, "found queda en false");
    CHECK(row.asset_uri == NULL, "el resto de los campos queda en NULL, no con basura");

    db_asset_row_free(&row);
    db_close(conn);
}

static void test_upsert_update_preserva_created_at(void) {
    printf("db_upsert_asset: un segundo upsert al mismo asset_uri actualiza, NO duplica, "
           "y preserva created_at\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;

    db_upsert_asset(conn, "torvalds", "linux", "linux", "{\"stars\":214000}", &err);

    /* Se fija created_at a un valor artificial y reconocible via SQL
     * crudo (bypaseando db_upsert_asset a proposito) para poder
     * distinguir con certeza "se preservo" de "coincidio porque los dos
     * upserts pasaron dentro del mismo segundo" -CURRENT_TIMESTAMP de
     * SQLite tiene resolucion de un segundo, y este test corre mucho
     * mas rapido que eso. */
    sqlite3_exec(conn,
                 "UPDATE assets SET created_at = '2020-01-01 00:00:00' "
                 "WHERE asset_uri = 'github://torvalds/linux';",
                 NULL, NULL, NULL);

    bool ok = db_upsert_asset(conn, "torvalds", "linux", "linux (actualizado)",
                               "{\"stars\":215000}", &err);
    CHECK(ok, "segundo upsert exitoso");
    CHECK(count_rows(conn) == 1,
          "sigue habiendo una sola fila (ON CONFLICT actualizo, no duplico)");

    DbAssetRow row;
    db_get_asset(conn, "torvalds", "linux", &row, &err);
    CHECK(strcmp(row.created_at, "2020-01-01 00:00:00") == 0,
          "created_at NO se toco en el UPDATE (sigue siendo el valor artificial)");
    CHECK(strcmp(row.updated_at, "2020-01-01 00:00:00") != 0,
          "updated_at SI se refresco a CURRENT_TIMESTAMP (ya no es el valor artificial)");
    CHECK(strcmp(row.title, "linux (actualizado)") == 0, "title se actualizo");
    CHECK(strstr(row.meta_payload, "215000") != NULL, "meta_payload se actualizo");

    db_asset_row_free(&row);
    db_close(conn);
}

static void test_dos_repos_distintos_no_colisionan(void) {
    printf("db_upsert_asset: dos repos distintos (mismo owner, distinto repo) no "
           "colisionan\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;

    db_upsert_asset(conn, "rails", "rails", "rails", "{\"name\":\"rails\"}", &err);
    db_upsert_asset(conn, "rails", "solid_queue", "solid_queue",
                     "{\"name\":\"solid_queue\"}", &err);

    CHECK(count_rows(conn) == 2, "quedan 2 filas, no se pisaron entre si");

    DbAssetRow row1, row2;
    db_get_asset(conn, "rails", "rails", &row1, &err);
    db_get_asset(conn, "rails", "solid_queue", &row2, &err);
    CHECK(row1.found && strcmp(row1.title, "rails") == 0, "primer repo recuperable");
    CHECK(row2.found && strcmp(row2.title, "solid_queue") == 0, "segundo repo recuperable");

    db_asset_row_free(&row1);
    db_asset_row_free(&row2);
    db_close(conn);
}

static void test_bind_seguro_contra_caracteres_especiales(void) {
    printf("db_upsert_asset: title/meta_payload con comillas simples y caracteres "
           "especiales (confirma binding parametrizado, no concatenacion de SQL)\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;

    const char *title_peligroso = "Repo con 'comillas' y -- comentario SQL; DROP TABLE assets;";
    bool ok = db_upsert_asset(conn, "test", "peligroso", title_peligroso,
                               "{\"description\":\"O'Brien's \\\"repo\\\"\"}", &err);
    CHECK(ok, "el upsert no falla ni se corrompe con caracteres especiales en el valor");
    CHECK(count_rows(conn) == 1, "la tabla 'assets' sigue existiendo (el DROP TABLE dentro "
                                  "del valor NO se ejecuto como SQL: no hubo query "
                                  "concatenada, el bind lo trato como dato)");

    DbAssetRow row;
    db_get_asset(conn, "test", "peligroso", &row, &err);
    CHECK(row.found && strcmp(row.title, title_peligroso) == 0,
          "el valor round-tripea exactamente igual, comillas incluidas");

    db_asset_row_free(&row);
    db_close(conn);
}

static void test_title_null(void) {
    printf("db_upsert_asset: title NULL (caso defensivo, en la practica siempre viene "
           "poblado desde RepoInfo->name)\n");
    sqlite3 *conn = open_memory_db();
    GitHubError err;

    bool ok = db_upsert_asset(conn, "sin", "titulo", NULL, "{}", &err);
    CHECK(ok, "upsert con title NULL no falla");

    DbAssetRow row;
    db_get_asset(conn, "sin", "titulo", &row, &err);
    CHECK(row.found && row.title == NULL,
          "title queda en NULL (bindeado con sqlite3_bind_null, no como string \"NULL\")");

    db_asset_row_free(&row);
    db_close(conn);
}

/* ------------------------------------------------------------------- */

int main(void) {
    printf("=== Unit tests: Sprint 3.4 (db, SQLite en memoria) ===\n\n");

    test_open_and_schema();
    test_upsert_insert_nuevo();
    test_get_asset_inexistente();
    test_upsert_update_preserva_created_at();
    test_dos_repos_distintos_no_colisionan();
    test_bind_seguro_contra_caracteres_especiales();
    test_title_null();

    printf("\n=== %s (%d fallos) ===\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}

