#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <stdbool.h>

#include "http_client.h" /* GitHubErrorCode, GitHubError, github_error_set */

/*
 * db.h — persistencia SQLite (Sprint 3.4). Replica db.py del prototipo:
 * crea el esquema "assets" si no existe, y hace upsert atomico por
 * asset_uri.
 *
 * Decision de diseno principal: el DDL del esquema esta EMBEBIDO como
 * string en db.c, no leido de un schema.sql en disco en runtime (que es
 * lo que hace el prototipo: SCHEMA_PATH = Path(__file__).parent /
 * "schema.sql"). Python puede apoyarse en __file__ porque el interprete
 * siempre sabe donde esta el .py que se esta ejecutando; C no tiene un
 * equivalente portable de __file__, y aunque lo hubiera (ej. leer
 * /proc/self/exe en Linux), atarse a "un archivo al lado del binario"
 * es fragil si alguien copia el binario compilado a otro directorio sin
 * llevarse schema.sql. Embeber el DDL hace que el binario final sea
 * autocontenido: sin dependencias de archivos en runtime mas alla de la
 * propia base de datos que crea (encaja con el criterio de exito del
 * README: "Ejecute completamente desde linea de comandos en Linux").
 * El DDL embebido se mantiene identico al de prototype/schema.sql /
 * README.md "Persistencia" a proposito -son la misma definicion, en dos
 * formatos distintos por necesidad de cada implementacion.
 */

#define DB_DEFAULT_PATH "github_client.db"

/*
 * Abre (o crea) la base SQLite en db_path y garantiza que el esquema
 * "assets" ya exista antes de devolver la conexion (equivalente a
 * get_connection() + _init_schema() del prototipo, fusionados en una
 * sola llamada: asi ningun otro codigo tiene que acordarse de
 * inicializar el esquema aparte). db_path puede ser ":memory:" (SQLite
 * en memoria, sin tocar disco -lo que usan los tests de este modulo).
 *
 * Devuelve true + *out_conn abierto, o false + *out_error poblado
 * (GH_ERR_INTERNAL) si sqlite3_open() o la creacion del esquema fallan.
 */
bool db_open(const char *db_path, sqlite3 **out_conn, GitHubError *out_error);

void db_close(sqlite3 *conn);

/*
 * Inserta o actualiza (UPSERT atomico por asset_uri = "github://{owner}/
 * {repo}") el registro consolidado de un repo. meta_payload_json debe
 * ser el JSON consolidado YA serializado (ver
 * json_parser_serialize_repo_info()) -este modulo no arma el JSON, solo
 * lo persiste tal cual se lo pasan (separacion de responsabilidades:
 * "Persistencia" en docs/architecture.md no incluye armar el payload).
 *
 * title se recibe como parametro aparte en vez de re-extraerse de
 * meta_payload_json (que implicaria volver a parsear el JSON solo para
 * sacar un campo que quien llama -core.c, Sprint 3.5- ya tiene a mano de
 * forma nativa en RepoInfo->name). El valor persistido es el mismo de
 * cualquier manera: RepoInfo->name ES lo que termina en el campo "name"
 * del JSON consolidado.
 *
 * Mapeo (igual que README.md, seccion "Persistencia"):
 *   asset_uri    -> github://{owner}/{repo}
 *   title        -> parametro title (puede ser NULL: la columna es
 *                    TEXT nullable)
 *   entity       -> "repository" (fijo)
 *   provider     -> "github" (fijo)
 *   meta_payload -> meta_payload_json tal cual
 *
 * created_at NO se toca en el UPDATE (conserva la fecha del primer alta,
 * ver DDL); updated_at se refresca a CURRENT_TIMESTAMP en cada upsert
 * -el DEFAULT de la columna solo aplica en el INSERT inicial, nunca en
 * updates posteriores (comportamiento estandar de SQLite, igual que en
 * el prototipo).
 *
 * Se usa una unica sentencia INSERT ... ON CONFLICT DO UPDATE (UPSERT
 * atomico, igual que el prototipo) en vez de un SELECT previo +
 * INSERT/UPDATE a mano.
 */
bool db_upsert_asset(sqlite3 *conn, const char *owner, const char *repo, const char *title,
                      const char *meta_payload_json, GitHubError *out_error);

/* Fila cruda de la tabla assets (equivalente al dict que devuelve
 * get_asset() en el prototipo). meta_payload viaja como texto JSON
 * crudo, sin deserializar -eso es cosa de quien llama, si lo necesita. */
typedef struct {
    bool found;
    char *asset_uri;    /* owned */
    char *title;         /* owned, puede ser NULL */
    char *entity;         /* owned */
    char *provider;        /* owned */
    char *created_at;       /* owned, formato datetime de SQLite (UTC) */
    char *updated_at;        /* owned */
    char *meta_payload;       /* owned, JSON crudo */
} DbAssetRow;

/*
 * Recupera un registro por asset_uri = "github://{owner}/{repo}".
 * Pensado para validacion manual y para tests (equivalente a
 * get_asset() del prototipo).
 *
 * Devuelve true + *out_row con found=true y los campos poblados si
 * existe; true + *out_row con found=false (resto de los campos en
 * NULL/vacio) si NO existe -no encontrar el registro NO es un error,
 * igual que get_asset() devolviendo None en Python-; o false +
 * *out_error poblado ante un error real de SQLite.
 */
bool db_get_asset(sqlite3 *conn, const char *owner, const char *repo, DbAssetRow *out_row,
                   GitHubError *out_error);

/* Libera los campos owned de *row (tolera row con found=false, y tolera
 * llamarse mas de una vez: deja *row en su estado "vacio" despues). */
void db_asset_row_free(DbAssetRow *row);

#endif /* DB_H */

