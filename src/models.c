#include "models.h"

#include <stdlib.h>

static void free_languages(RepoInfo *info) {
    if (info->languages != NULL) {
        for (size_t i = 0; i < info->languages_count; i++) {
            free(info->languages[i].name);
        }
        free(info->languages);
        info->languages = NULL;
        info->languages_count = 0;
    }
}

RepoInfo *repo_info_new(void) {
    /* calloc, no malloc: deja todos los punteros en NULL, los int64_t en
     * 0 y contributors_count.present en false (bool false == byte 0) sin
     * tener que inicializar campo por campo a mano. */
    return calloc(1, sizeof(RepoInfo));
}

void repo_info_free(RepoInfo *info) {
    if (info == NULL) {
        return;
    }
    free(info->name);
    free(info->owner);
    free(info->description);
    free(info->default_branch);
    free_languages(info);
    free(info);
}

bool repo_info_set_languages(RepoInfo *info, size_t count) {
    free_languages(info);

    if (count == 0) {
        return true; /* languages ya quedó NULL / languages_count en 0 */
    }

    Language *arr = calloc(count, sizeof(Language));
    if (arr == NULL) {
        return false;
    }

    info->languages = arr;
    info->languages_count = count;
    return true;
}

