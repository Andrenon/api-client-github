#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debe mantenerse identico a prototype/schema.sql / README.md,
 * seccion "Persistencia" -misma definicion, distinto formato por
 * necesidad de cada implementacion (ver nota de diseno en db.h). */
static const char *ASSETS_SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS assets ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    asset_uri TEXT UNIQUE NOT NULL,"
    "    title TEXT,"
    "    entity TEXT NOT NULL,"
    "    provider TEXT NOT NULL,"
    "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
    "    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
    "    meta_payload TEXT"
    ");";

static char *build_asset_uri(const char *owner, const char *repo) {
    size_t needed = strlen("github://") + strlen(owner) + 1 /* '/' */ + strlen(repo) + 1;
    char *uri = malloc(needed);
    if (uri == NULL) {
        return NULL;
    }
    snprintf(uri, needed, "github://%s/%s", owner, repo);
    return uri;
}

/* sqlite3_column_text devuelve NULL para una columna SQL NULL (ej. title
 * cuando nunca se seteo): se replica tal cual, sin convertir a "" -sigue
 * la misma semantica que el dict de Python (result["title"] puede ser
 * None). */
static char *dup_column_text(sqlite3_stmt *stmt, int column_index) {
    const unsigned char *text = sqlite3_column_text(stmt, column_index);
    if (text == NULL) {
        return NULL;
    }
    return strdup((const char *)text);
}

bool db_open(const char *db_path, sqlite3 **out_conn, GitHubError *out_error) {
    *out_conn = NULL;
    sqlite3 *conn = NULL;

    int rc = sqlite3_open(db_path, &conn);
    if (rc != SQLITE_OK) {
        char msg[160];
        snprintf(msg, sizeof(msg), "No se pudo abrir la base SQLite '%s': %s", db_path,
                 sqlite3_errmsg(conn));
        github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
        /* sqlite3_open() puede devolver un handle valido aunque rc no
         * sea SQLITE_OK (para poder leer sqlite3_errmsg de el); hay que
         * cerrarlo igual para no perderlo. */
        sqlite3_close(conn);
        return false;
    }

    char *errmsg = NULL;
    rc = sqlite3_exec(conn, ASSETS_SCHEMA_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        char msg[160];
        snprintf(msg, sizeof(msg), "No se pudo crear el esquema 'assets': %s",
                 errmsg != NULL ? errmsg : "error desconocido");
        github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
        sqlite3_free(errmsg); /* mensajes de sqlite3_exec se liberan con
                                  sqlite3_free, no con free() */
        sqlite3_close(conn);
        return false;
    }

    *out_conn = conn;
    return true;
}

void db_close(sqlite3 *conn) {
    if (conn != NULL) {
        sqlite3_close(conn);
    }
}

bool db_upsert_asset(sqlite3 *conn, const char *owner, const char *repo, const char *title,
                      const char *meta_payload_json, GitHubError *out_error) {
    char *asset_uri = build_asset_uri(owner, repo);
    if (asset_uri == NULL) {
        github_error_set(out_error, GH_ERR_INTERNAL, 0, "Sin memoria al construir asset_uri.");
        return false;
    }

    /* entity/provider van fijos en el SQL (siempre "repository"/"github"
     * en este proyecto: ver README, "No se contempla" -no hay otros
     * providers-), no como parametros bindeados sin necesidad. */
    static const char *SQL = "INSERT INTO assets (asset_uri, title, entity, provider, "
                              "meta_payload) "
                              "VALUES (?, ?, 'repository', 'github', ?) "
                              "ON CONFLICT(asset_uri) DO UPDATE SET "
                              "    title = excluded.title, "
                              "    meta_payload = excluded.meta_payload, "
                              "    updated_at = CURRENT_TIMESTAMP;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(conn, SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char msg[160];
        snprintf(msg, sizeof(msg), "No se pudo preparar el UPSERT: %s", sqlite3_errmsg(conn));
        github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
        free(asset_uri);
        return false;
    }

    /* SQLITE_TRANSIENT le indica a sqlite3 que haga su propia copia
     * interna de cada string bindeado. A diferencia de Python
     * (conn.execute(sql, dict) copia los valores sin que el programador
     * tenga que pensar en su ciclo de vida), la API C de SQLite NO copia
     * por defecto: con SQLITE_STATIC asumiria que el puntero sigue vivo
     * hasta sqlite3_step(), y asset_uri se libera ANTES de eso en esta
     * misma funcion -habria sido un use-after-free silencioso, del
     * mismo estilo al que ya encontre en json_parser.c (Sprint 3.3). */
    sqlite3_bind_text(stmt, 1, asset_uri, -1, SQLITE_TRANSIENT);
    if (title != NULL) {
        sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_text(stmt, 3, meta_payload_json, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    bool ok = (rc == SQLITE_DONE);
    if (!ok) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Fallo el UPSERT: %s", sqlite3_errmsg(conn));
        github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
    }

    /* No hace falta un sqlite3_exec(conn, "COMMIT") aparte: sin una
     * transaccion explicita (BEGIN) abierta, sqlite3 esta en modo
     * autocommit y cada sqlite3_step() que termina una sentencia se
     * confirma sola -a diferencia del modulo sqlite3 de Python, que usa
     * transacciones implicitas diferidas y requiere conn.commit() a
     * mano incluso para una sola sentencia. */
  /*
   * Operación de Múltiples consultas en C
   * // 1. Desactivas autocommit iniciando la transacción
   * sqlite3_exec(conn, "BEGIN TRANSACTION;", NULL, NULL, NULL);
   * 
   * // 2. Ejecutas la primera consulta (ej. insertar asset)
   * if (fallo_al_insertar_asset) {
   *    // Cancelas TODO lo hecho dentro del BEGIN
   *    sqlite3_exec(conn, "ROLLBACK;", NULL, NULL, NULL); 
   *    return false;
   * }
   * 
   * // 3. Ejecutas la segunda consulta (ej. insertar lenguajes)
   * if (fallo_al_insertar_lenguajes) {
   *    // Deshaces también el asset insertado arriba
   *    sqlite3_exec(conn, "ROLLBACK;", NULL, NULL, NULL); 
   *    return false;
   * }
   * 
   * // 4. Si todo salió bien, confirmas los cambios
   * sqlite3_exec(conn, "COMMIT;", NULL, NULL, NULL);
   * return true;
   */

    sqlite3_finalize(stmt);
    free(asset_uri);
    return ok;
}

bool db_get_asset(sqlite3 *conn, const char *owner, const char *repo, DbAssetRow *out_row,
                   GitHubError *out_error) {
    memset(out_row, 0, sizeof(*out_row));

    char *asset_uri = build_asset_uri(owner, repo);
    if (asset_uri == NULL) {
        github_error_set(out_error, GH_ERR_INTERNAL, 0, "Sin memoria al construir asset_uri.");
        return false;
    }

    static const char *SQL = "SELECT asset_uri, title, entity, provider, created_at, "
                              "updated_at, meta_payload "
                              "FROM assets WHERE asset_uri = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(conn, SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char msg[160];
        snprintf(msg, sizeof(msg), "No se pudo preparar el SELECT: %s", sqlite3_errmsg(conn));
        github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
        free(asset_uri);
        return false;
    }

    sqlite3_bind_text(stmt, 1, asset_uri, -1, SQLITE_TRANSIENT);
    free(asset_uri);

    rc = sqlite3_step(stmt);
    bool ok = true;

    if (rc == SQLITE_ROW) {
        out_row->found = true;
        out_row->asset_uri = dup_column_text(stmt, 0);
        out_row->title = dup_column_text(stmt, 1);
        out_row->entity = dup_column_text(stmt, 2);
        out_row->provider = dup_column_text(stmt, 3);
        out_row->created_at = dup_column_text(stmt, 4);
        out_row->updated_at = dup_column_text(stmt, 5);
        out_row->meta_payload = dup_column_text(stmt, 6);
    } else if (rc == SQLITE_DONE) {
        out_row->found = false; /* no existe: no es un error, igual que
                                    get_asset() -> None en el prototipo */
    } else {
        char msg[160];
        snprintf(msg, sizeof(msg), "Fallo el SELECT: %s", sqlite3_errmsg(conn));
        github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
        ok = false;
    }

    sqlite3_finalize(stmt);
    return ok;
}

void db_asset_row_free(DbAssetRow *row) {
    if (row == NULL) {
        return;
    }
    free(row->asset_uri);
    free(row->title);
    free(row->entity);
    free(row->provider);
    free(row->created_at);
    free(row->updated_at);
    free(row->meta_payload);
    memset(row, 0, sizeof(*row));
}

