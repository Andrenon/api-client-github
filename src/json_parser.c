#include "json_parser.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------- */
/* Helper de validacion: reporta "campo ausente o de tipo invalido" con   */
/* el nombre del campo en el mensaje (a diferencia de un KeyError de      */
/* Python, que ya trae el nombre de la clave solo, este mensaje tambien   */
/* dice que tipo se esperaba, para que sea mas facil de diagnosticar      */
/* contra una respuesta real inesperada).                                 */
/* --------------------------------------------------------------------- */

static GitHubErrorCode fail_field(GitHubError *out_error, const char *field,
                                   const char *expected_type) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "Campo '%s' ausente o de tipo invalido en la respuesta (se esperaba %s).", field,
             expected_type);
    github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
    return GH_ERR_INTERNAL;
}

/* --------------------------------------------------------------------- */
/* GET /repos/{owner}/{repo}                                              */
/* --------------------------------------------------------------------- */

GitHubErrorCode json_parser_parse_repo(const char *json_body, RepoInfo *info,
                                        GitHubError *out_error) {
    cJSON *root = cJSON_Parse(json_body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        github_error_set(out_error, GH_ERR_INTERNAL, 0,
                          "La respuesta de /repos/{owner}/{repo} no es un objeto JSON valido.");
        return GH_ERR_INTERNAL;
    }

    GitHubErrorCode result;

    /* --- Fase 1: validar TODOS los campos requeridos antes de tocar
     * *info. Asi, ante un error, *info queda exactamente como estaba
     * (nunca a medio popular) -- ver nota en json_parser.h. */

    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsNumber(id)) {
        result = fail_field(out_error, "id", "number");
        goto done;
    }

    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == NULL) {
        result = fail_field(out_error, "name", "string");
        goto done;
    }

    cJSON *owner_obj = cJSON_GetObjectItemCaseSensitive(root, "owner");
    cJSON *owner_login =
        cJSON_IsObject(owner_obj) ? cJSON_GetObjectItemCaseSensitive(owner_obj, "login") : NULL;
    if (!cJSON_IsString(owner_login) || owner_login->valuestring == NULL) {
        result = fail_field(out_error, "owner.login", "string");
        goto done;
    }

    cJSON *stars = cJSON_GetObjectItemCaseSensitive(root, "stargazers_count");
    if (!cJSON_IsNumber(stars)) {
        result = fail_field(out_error, "stargazers_count", "number");
        goto done;
    }

    cJSON *forks = cJSON_GetObjectItemCaseSensitive(root, "forks_count");
    if (!cJSON_IsNumber(forks)) {
        result = fail_field(out_error, "forks_count", "number");
        goto done;
    }

    cJSON *watchers = cJSON_GetObjectItemCaseSensitive(root, "watchers_count");
    if (!cJSON_IsNumber(watchers)) {
        result = fail_field(out_error, "watchers_count", "number");
        goto done;
    }

    cJSON *default_branch = cJSON_GetObjectItemCaseSensitive(root, "default_branch");
    if (!cJSON_IsString(default_branch) || default_branch->valuestring == NULL) {
        result = fail_field(out_error, "default_branch", "string");
        goto done;
    }

    /* description: unico campo nullable-por-diseño. Ausente o JSON null
     * -> cJSON_IsString da false en ambos casos -> has_description queda
     * en false, NO es un error (ver docstring en json_parser.h). */
    cJSON *description = cJSON_GetObjectItemCaseSensitive(root, "description");
    bool has_description = cJSON_IsString(description) && description->valuestring != NULL;

    /* --- Fase 2: todo validado. Copiar a memoria propia (strdup puede
     * fallar por falta de memoria; se chequea cada uno). --- */

    char *name_copy = strdup(name->valuestring);
    char *owner_copy = strdup(owner_login->valuestring);
    char *branch_copy = strdup(default_branch->valuestring);
    char *description_copy = has_description ? strdup(description->valuestring) : NULL;

    if (name_copy == NULL || owner_copy == NULL || branch_copy == NULL ||
        (has_description && description_copy == NULL)) {
        free(name_copy);
        free(owner_copy);
        free(branch_copy);
        free(description_copy);
        github_error_set(out_error, GH_ERR_INTERNAL, 0,
                          "Sin memoria al copiar los campos de RepoInfo.");
        result = GH_ERR_INTERNAL;
        goto done;
    }

    /* Recien aca se muta *info, y de forma atomica: si llegamos hasta
     * este punto, TODO salio bien y se aplica de una. free() del valor
     * previo antes de reasignar por si json_parser_parse_repo() se
     * llama mas de una vez sobre el mismo RepoInfo (no deberia pasar en
     * el flujo normal de core.c, pero evita un leak si pasara). */
    free(info->name);
    info->name = name_copy;
    free(info->owner);
    info->owner = owner_copy;
    free(info->default_branch);
    info->default_branch = branch_copy;
    free(info->description);
    info->description = description_copy;

    info->id = (int64_t)id->valuedouble;
    info->stars = (int64_t)stars->valuedouble;
    info->forks = (int64_t)forks->valuedouble;
    info->watchers = (int64_t)watchers->valuedouble;

    result = GH_OK;

done:
    cJSON_Delete(root);
    return result;
}

/* --------------------------------------------------------------------- */
/* GET /repos/{owner}/{repo}/languages                                    */
/* --------------------------------------------------------------------- */

GitHubErrorCode json_parser_parse_languages(const char *json_body, RepoInfo *info,
                                             GitHubError *out_error) {
    cJSON *root = cJSON_Parse(json_body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        github_error_set(
            out_error, GH_ERR_INTERNAL, 0,
            "La respuesta de /repos/{owner}/{repo}/languages no es un objeto JSON valido.");
        return GH_ERR_INTERNAL;
    }

    /* cJSON_GetArraySize funciona tanto para arrays como para objetos
     * (cuenta los hijos del nodo, sea cual sea su tipo) -no hace falta
     * una funcion "GetObjectSize" aparte, cJSON no la tiene. */
    int count = cJSON_GetArraySize(root);

    if (!repo_info_set_languages(info, (size_t)count)) {
        cJSON_Delete(root);
        github_error_set(out_error, GH_ERR_INTERNAL, 0,
                          "Sin memoria al reservar el array de languages.");
        return GH_ERR_INTERNAL;
    }

    size_t i = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, root) {
        if (!cJSON_IsNumber(entry)) {
            /* Defensivo: no deberia pasar contra la API real, pero si
             * algun lenguaje viniera con un valor no numerico, se corta
             * con error explicito en vez de guardar basura en
             * silencio. Se limpia lo que se llego a poblar para no
             * dejar el array a medio inicializar.
             *
             * OJO con el orden: el mensaje se arma ANTES de
             * cJSON_Delete(root). entry es hijo de root -borrar root
             * libera tambien entry y su campo ->string-, asi que leer
             * entry->string despues de borrar el arbol es
             * use-after-free. Lo detecto corriendo el test unitario:
             * el mensaje salia con basura en vez del nombre real del
             * lenguaje. */
            char msg[160];
            snprintf(msg, sizeof(msg), "El lenguaje '%s' no tiene un valor numerico de bytes.",
                     entry->string != NULL ? entry->string : "?");
            repo_info_set_languages(info, 0);
            cJSON_Delete(root);
            github_error_set(out_error, GH_ERR_INTERNAL, 0, msg);
            return GH_ERR_INTERNAL;
        }

        char *name_copy = strdup(entry->string != NULL ? entry->string : "");
        if (name_copy == NULL) {
            repo_info_set_languages(info, 0);
            cJSON_Delete(root);
            github_error_set(out_error, GH_ERR_INTERNAL, 0,
                              "Sin memoria al copiar el nombre de un lenguaje.");
            return GH_ERR_INTERNAL;
        }

        info->languages[i].name = name_copy;
        info->languages[i].bytes = (int64_t)entry->valuedouble;
        i++;
    }

    cJSON_Delete(root);
    return GH_OK;
}

/* --------------------------------------------------------------------- */
/* RepoInfo -> JSON (sentido inverso, Sprint 3.4)                         */
/* --------------------------------------------------------------------- */

bool json_parser_serialize_repo_info(const RepoInfo *info, bool pretty, char **out_json,
                                      GitHubError *out_error) {
    *out_json = NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        github_error_set(out_error, GH_ERR_INTERNAL, 0,
                          "Sin memoria al armar el JSON consolidado.");
        return false;
    }

    /* Mismo orden que el "Esquema JSON Consolidado" de README.md, para
     * que un diff visual contra un ejemplo del contrato sea directo. */
    cJSON_AddNumberToObject(root, "id", (double)info->id);
    cJSON_AddStringToObject(root, "name", info->name != NULL ? info->name : "");
    cJSON_AddStringToObject(root, "owner", info->owner != NULL ? info->owner : "");

    if (info->description != NULL) {
        cJSON_AddStringToObject(root, "description", info->description);
    } else {
        cJSON_AddNullToObject(root, "description");
    }

    cJSON_AddNumberToObject(root, "stars", (double)info->stars);
    cJSON_AddNumberToObject(root, "forks", (double)info->forks);
    cJSON_AddNumberToObject(root, "watchers", (double)info->watchers);
    cJSON_AddStringToObject(root, "default_branch",
                             info->default_branch != NULL ? info->default_branch : "");

    cJSON *languages_obj = cJSON_AddObjectToObject(root, "languages");
    if (languages_obj != NULL) {
        for (size_t i = 0; i < info->languages_count; i++) {
            cJSON_AddNumberToObject(languages_obj, info->languages[i].name,
                                     (double)info->languages[i].bytes);
        }
    }

    if (info->contributors_count.present) {
        cJSON_AddNumberToObject(root, "contributors_count",
                                 (double)info->contributors_count.value);
    } else {
        cJSON_AddNullToObject(root, "contributors_count");
    }

    cJSON_AddNumberToObject(root, "branches_count", (double)info->branches_count);
    cJSON_AddNumberToObject(root, "releases_count", (double)info->releases_count);

    char *json_text = pretty ? cJSON_Print(root) : cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_text == NULL) {
        github_error_set(out_error, GH_ERR_INTERNAL, 0,
                          "Sin memoria al imprimir el JSON consolidado.");
        return false;
    }

    *out_json = json_text; /* cJSON_Print* usa malloc(), lo puede liberar
                             * el caller con free() normal */
    return true;
}

