/*
 * main.c — CLI (Sprint 3.6). Replica github_client.py: parseo de
 * <owner>/<repo> [--json], invoca a core_consolidate(), persiste en
 * SQLite, imprime el resultado (texto legible o --json), y avisa
 * proactivamente el estado del rate limit al final.
 *
 * Frontera con core.c: exactamente la misma que entre github_client.py
 * y consolidate.py en el prototipo -core_consolidate() no sabe nada de
 * argv, stdout/stderr, ni SQLite; todo eso vive aca.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core.h"
#include "db.h"
#include "json_parser.h"
#include "models.h"

/* No se deriva del argv[0] real (aunque C lo permite via basename()):
 * se fija al nombre documentado en el contrato de software (README.md,
 * seccion "Entrada": "github-client <owner>/<repo>"), asi el mensaje de
 * uso siempre coincide con el contrato sin importar como se invoco el
 * binario en la practica (./build/github-client, un symlink, etc.). */
#define PROGRAM_NAME "github-client"

typedef struct {
    char *owner; /* owned */
    char *repo;   /* owned */
    bool as_json;
} CliArgs;

static void cli_args_free(CliArgs *args) {
    free(args->owner);
    free(args->repo);
    args->owner = NULL;
    args->repo = NULL;
}

static void print_usage(void) {
    fprintf(stderr, "Uso: %s <owner>/<repo> [--json]\n", PROGRAM_NAME);
    fprintf(stderr, "Ejemplo: %s torvalds/linux\n", PROGRAM_NAME);
}

/*
 * Replica parse_args() del prototipo: exactamente 1 o 2 argumentos,
 * el primero con EXACTAMENTE un '/' (ni "ownerrepo" ni "a/b/c"), owner
 * y repo no vacios, segundo argumento (si existe) tiene que ser
 * "--json" literal.
 *
 * Devuelve true + *out_args poblado si es valido. Devuelve false si no
 * -ya imprimio el mensaje correspondiente a stderr, el caller solo
 * tiene que salir con exit code 1-.
 */
static bool parse_args(int argc, char *argv[], CliArgs *out_args) {
    out_args->owner = NULL;
    out_args->repo = NULL;
    out_args->as_json = false;

    int n_args = argc - 1; /* sin contar argv[0] */
    if (n_args != 1 && n_args != 2) {
        print_usage();
        return false;
    }

    const char *spec = argv[1];
    int slash_count = 0;
    const char *slash_pos = NULL;
    for (const char *p = spec; *p != '\0'; p++) {
        if (*p == '/') {
            slash_count++;
            slash_pos = p;
        }
    }
    if (slash_count != 1) {
        print_usage();
        return false;
    }

    size_t owner_len = (size_t)(slash_pos - spec);
    size_t repo_len = strlen(slash_pos + 1);
    if (owner_len == 0 || repo_len == 0) {
        fprintf(stderr, "Formato inválido. Usá: <owner>/<repo>\n");
        return false;
    }

    char *owner = malloc(owner_len + 1);
    char *repo = malloc(repo_len + 1);
    if (owner == NULL || repo == NULL) {
        free(owner);
        free(repo);
        fprintf(stderr, "Sin memoria.\n");
        return false;
    }
    memcpy(owner, spec, owner_len);
    owner[owner_len] = '\0';
    strcpy(repo, slash_pos + 1);

    bool as_json = false;
    if (n_args == 2) {
        if (strcmp(argv[2], "--json") == 0) {
            as_json = true;
        } else {
            fprintf(stderr, "Opción desconocida: %s\n", argv[2]);
            free(owner);
            free(repo);
            return false;
        }
    }

    out_args->owner = owner;
    out_args->repo = repo;
    out_args->as_json = as_json;
    return true;
}

/*
 * Escribe "HH:MM:SS" en hora local en out_buf (>= 9 bytes) y devuelve
 * true, o devuelve false (out_buf sin tocar) si reset_at no esta
 * disponible o localtime_r/strftime fallan. Equivalente a
 * _format_reset() del prototipo (datetime.fromtimestamp + strftime).
 * No usa un buffer estatico interno (a diferencia del idiom clasico de
 * C tipo ctime()): el caller provee el suyo, asi no hay estado oculto
 * ni riesgo de que una segunda llamada pise el resultado de la primera
 * antes de usarlo.
 */
static bool format_reset(bool has_reset_at, int64_t reset_at, char *out_buf,
                          size_t out_buf_size) {
    if (!has_reset_at) {
        return false;
    }
    time_t t = (time_t)reset_at;
    struct tm local_tm;
    if (localtime_r(&t, &local_tm) == NULL) {
        return false;
    }
    if (strftime(out_buf, out_buf_size, "%H:%M:%S", &local_tm) == 0) {
        return false;
    }
    return true;
}

/*
 * Avisa proactivamente cuanto presupuesto de rate limit queda.
 * Replica report_rate_limit(): si la consulta a /rate_limit falla, se
 * ignora en silencio (no vale la pena frenar el flujo principal, que ya
 * termino exitosamente, por esto).
 */
static void report_rate_limit(const char *token) {
    RateLimitStatus status;
    GitHubError err;
    if (http_client_get_rate_limit_status(token, &status, &err) != GH_OK) {
        return;
    }

    fprintf(stderr, "[rate limit] %lld/%lld solicitudes restantes esta hora.\n",
            (long long)status.remaining, (long long)status.limit);

    if (status.remaining <= 5) {
        char reset_buf[16];
        bool has_reset = format_reset(true, status.reset, reset_buf, sizeof(reset_buf));
        if (has_reset) {
            fprintf(stderr,
                    "[rate limit] Quedan pocas solicitudes disponibles (se reinicia a las "
                    "%s).\n",
                    reset_buf);
        } else {
            fprintf(stderr, "[rate limit] Quedan pocas solicitudes disponibles.\n");
        }
        if (token == NULL) {
            fprintf(stderr,
                    "[rate limit] Sugerencia: definí GITHUB_TOKEN para subir el límite de "
                    "60 a 5000 solicitudes/hora.\n");
        }
    }
}

/* Traduce un GitHubErrorCode de core_consolidate() al mismo texto de
 * error que el bloque try/except de main() en el prototipo. */
static void print_core_error(GitHubErrorCode code, const GitHubError *err, const char *owner,
                              const char *repo, const char *token) {
    switch (code) {
        case GH_ERR_NOT_FOUND:
            fprintf(stderr, "Error: el repositorio '%s/%s' no existe o no es accesible (404).\n",
                    owner, repo);
            break;
        case GH_ERR_INVALID_TOKEN:
            fprintf(stderr,
                    "Error: el token provisto (GITHUB_TOKEN) es inválido o expiró (401).\n");
            break;
        case GH_ERR_RATE_LIMIT_EXCEEDED: {
            fprintf(stderr, "Error: límite de solicitudes excedido (%ld).\n", err->status_code);
            char reset_buf[16];
            if (format_reset(err->has_reset_at, err->reset_at, reset_buf, sizeof(reset_buf))) {
                fprintf(stderr, "El límite se reinicia a las %s.\n", reset_buf);
            }
            if (token == NULL) {
                fprintf(stderr,
                        "Sugerencia: definí GITHUB_TOKEN para subir el límite de 60 a 5000 "
                        "solicitudes/hora.\n");
            }
            break;
        }
        case GH_ERR_FORBIDDEN:
            fprintf(stderr, "Error: acceso prohibido (403). Verificá los permisos del token.\n");
            break;
        default:
            /* Cubre GH_ERR_NETWORK, GH_ERR_UNEXPECTED_STATUS, GH_ERR_INTERNAL, y
             * GH_ERR_RESOURCE_TOO_LARGE -este ultimo en la practica nunca deberia
             * llegar hasta aca (core_consolidate() siempre lo intercepta para el
             * caso puntual de contributors, ver core.h), pero se cubre igual,
             * replicando el catch-all final "except GitHubAPIError" del
             * prototipo. */
            fprintf(stderr, "Error inesperado [%ld]: %s\n", err->status_code, err->message);
            break;
    }
}

/* Formato de salida legible (sin --json). Replica el bloque final de
 * main() en el prototipo campo por campo. */
static void print_human_summary(const RepoInfo *info) {
    printf("%s (%s)\n", info->name, info->owner);
    printf("  descripción: %s\n", info->description != NULL ? info->description : "-");
    printf("  stars: %lld | forks: %lld | watchers: %lld | rama principal: %s\n",
           (long long)info->stars, (long long)info->forks, (long long)info->watchers,
           info->default_branch);

    if (info->languages_count == 0) {
        printf("  lenguajes: -\n");
    } else {
        printf("  lenguajes: ");
        for (size_t i = 0; i < info->languages_count; i++) {
            printf("%s%s", i > 0 ? ", " : "", info->languages[i].name);
        }
        printf("\n");
    }

    if (info->contributors_count.present) {
        printf("  contributors: %lld | branches: %lld | releases: %lld\n",
               (long long)info->contributors_count.value, (long long)info->branches_count,
               (long long)info->releases_count);
    } else {
        printf("  contributors: N/D (repo con demasiado historial para que GitHub lo calcule "
               "vía API) | branches: %lld | releases: %lld\n",
               (long long)info->branches_count, (long long)info->releases_count);
    }
}

int main(int argc, char *argv[]) {
    CliArgs args = {0};
    int exit_code = 0;
    bool curl_initialized = false;
    RepoInfo *info = NULL;
    char *meta_json = NULL;
    char *pretty_json = NULL;
    sqlite3 *conn = NULL;
    GitHubError err;

    if (!parse_args(argc, argv, &args)) {
        exit_code = 1;
        goto done;
    }

    const char *token = getenv("GITHUB_TOKEN");

    if (!http_client_global_init()) {
        fprintf(stderr, "No se pudo inicializar libcurl.\n");
        exit_code = 1;
        goto done;
    }
    curl_initialized = true;

    fprintf(stderr, "Consultando github://%s/%s ...\n", args.owner, args.repo);

    GitHubErrorCode code = core_consolidate(args.owner, args.repo, token, &info, &err);
    if (code != GH_OK) {
        print_core_error(code, &err, args.owner, args.repo, token);
        exit_code = 1;
        goto done;
    }

    if (!json_parser_serialize_repo_info(info, false, &meta_json, &err)) {
        fprintf(stderr, "Error interno: no se pudo serializar el resultado: %s\n", err.message);
        exit_code = 1;
        goto done;
    }

    if (!db_open(DB_DEFAULT_PATH, &conn, &err)) {
        fprintf(stderr, "Error: no se pudo abrir la base de datos: %s\n", err.message);
        exit_code = 1;
        goto done;
    }

    if (!db_upsert_asset(conn, args.owner, args.repo, info->name, meta_json, &err)) {
        fprintf(stderr, "Error: no se pudo persistir en SQLite: %s\n", err.message);
        exit_code = 1;
        goto done;
    }

    fprintf(stderr, "Persistido en SQLite como github://%s/%s\n", args.owner, args.repo);

    if (args.as_json) {
        if (json_parser_serialize_repo_info(info, true, &pretty_json, &err)) {
            printf("%s\n", pretty_json);
        } else {
            fprintf(stderr, "Advertencia: no se pudo generar la salida --json: %s\n",
                    err.message);
        }
    } else {
        print_human_summary(info);
    }

    report_rate_limit(token);

done:
    if (conn != NULL) {
        db_close(conn);
    }
    free(pretty_json);
    free(meta_json);
    repo_info_free(info);
    if (curl_initialized) {
        http_client_global_cleanup();
    }
    cli_args_free(&args);
    return exit_code;
}

