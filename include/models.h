#ifndef MODELS_H
#define MODELS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * models.h — structs compartidos del dominio.
 *
 * Sprint 3.1 (docs/workplan.md) menciona structs para RepoInfo, Language,
 * Contributor, Release y Branch. Acá se definen solo RepoInfo y Language.
 *
 * Motivo (ver docs/notas-implementacion.md y prototype/endpoints/): el
 * prototipo Python — que es la especificación de comportamiento a
 * replicar, ya validada y congelada en Fase 2 — nunca parsea campos
 * individuales de un contributor, release o branch. Los tres se resuelven
 * con http_client.get_paginated_count() (per_page=1 + header Link), que
 * devuelve un entero. Definir structs Contributor/Release/Branch acá
 * sería modelar datos que ningún parser va a llenar. branches_count y
 * releases_count quedan como campos int64_t simples dentro de RepoInfo,
 * igual que en el esquema JSON consolidado del contrato de software
 * (README.md).
 */

/*
 * Un lenguaje detectado en el repo (nombre + bytes de código).
 *
 * A diferencia de contributors/releases/branches, GET .../languages NO
 * está paginado por count: la respuesta ya es el objeto completo
 * {lenguaje: bytes}, y consolidate.py lo vuelca tal cual al JSON final.
 * Por eso este es el único de los cuatro endpoints complementarios que
 * sí necesita un struct por entrada.
 */
typedef struct {
    char *name;    /* owned (heap), ej. "C", "Assembly" */
    int64_t bytes; /* ver nota de tipos más abajo sobre el uso de int64_t */
} Language;

/*
 * contributors_count puede no existir: docs/notas-implementacion.md #1
 * documenta que repos con historial muy grande (torvalds/linux,
 * chromium/chromium) hacen que GitHub devuelva 403 "too large to list..."
 * para /contributors, sin importar el token. Python lo modela con
 * Optional[int] = None.
 *
 * C no tiene Optional nativo. La alternativa de "sentinel value" (ej. -1
 * = "sin dato") se descartó a propósito: es fácil que algún call site se
 * olvide de chequearlo y trate -1 como un conteo real, y no hay forma de
 * que el compilador lo avise. Un struct explícito con flag `present` es
 * más verboso pero deja la ausencia de dato imposible de ignorar por
 * accidente en cada uso (count.value solo es válido si count.present).
 */
typedef struct {
    bool present;
    int64_t value; /* válido solo si present == true */
} OptionalCount;

/*
 * RepoInfo consolida los 5 endpoints en una sola estructura: es el
 * equivalente en C del dict que arma consolidate.py (y, en definitiva,
 * del "Esquema JSON Consolidado" del README).
 *
 * Gestión de memoria: todos los punteros son "owned" por el RepoInfo que
 * los contiene. Quien puebla un RepoInfo (json_parser en Sprint 3.3,
 * orquestado desde core.c en Sprint 3.5) es responsable de asignarles
 * memoria dinámica (malloc/strdup); repo_info_free() se encarga de
 * liberar todo. En Python esto es invisible (el GC libera el dict y sus
 * strings solo); en C es responsabilidad explícita del código, y por eso
 * existen repo_info_new()/repo_info_free() como constructor/destructor
 * simétricos en vez de dejar que cada sprint improvise su propia forma
 * de reservar y liberar.
 *
 * `description` puede ser NULL: en el JSON de GitHub el campo viaja como
 * `null` cuando el repo no tiene descripción — mismo caso que
 * repo_data.get("description") devolviendo None en Python. El resto de
 * los punteros (name, owner, default_branch) el contrato los da siempre
 * presentes, así que no se modela su ausencia.
 */
typedef struct {
    int64_t id;
    char *name;           /* owned, nunca NULL una vez poblado */
    char *owner;           /* owned, nunca NULL una vez poblado */
    char *description;     /* owned, puede ser NULL */
    int64_t stars;
    int64_t forks;
    int64_t watchers;
    char *default_branch;  /* owned, nunca NULL una vez poblado */

    Language *languages;   /* owned array, puede tener length 0 */
    size_t languages_count;

    OptionalCount contributors_count; /* puede estar ausente, ver arriba */
    int64_t branches_count;
    int64_t releases_count;
} RepoInfo;

/*
 * Nota de tipos: id/stars/forks/watchers/bytes/branches_count/
 * releases_count usan int64_t (no int ni long a secas) por dos motivos:
 *   1. cJSON (Sprint 3.3) representa todo número JSON internamente como
 *      double; al extraerlo a un entero de 64 bits no se pierde
 *      precisión para ningún valor real de la API de GitHub (stars,
 *      forks, bytes de lenguaje, etc. están lejos del límite de
 *      precisión entera de un double, 2^53).
 *   2. `long` es de 64 bits en Linux x86_64 (este proyecto es
 *      Linux-only, según el README), pero es de 32 bits en otras
 *      plataformas (ej. Windows/LLP64) — usar int64_t deja el ancho
 *      fijo en el propio tipo en vez de depender del compilador/SO.
 */

/*
 * Crea un RepoInfo con todos los campos en su estado "vacío": punteros en
 * NULL, contadores en 0, contributors_count.present en false. No reserva
 * memoria para campos individuales — eso lo hace cada parser a medida
 * que conoce el dato real (Sprint 3.3 en adelante).
 *
 * Devuelve NULL si falla la reserva (caller debe chequearlo).
 */
RepoInfo *repo_info_new(void);

/*
 * Libera un RepoInfo y todo lo que posee (strings, array de languages).
 * Tolera info == NULL (no-op), igual que free() estándar.
 */
void repo_info_free(RepoInfo *info);

/*
 * Reserva un array de `count` Language asociado a `info`, liberando antes
 * cualquier array previo (así se puede llamar más de una vez sin generar
 * un leak). Cada Language queda en su estado vacío (name = NULL,
 * bytes = 0) para que el caller la termine de poblar.
 *
 * Devuelve false si falla la reserva (info queda con languages = NULL,
 * languages_count = 0; nunca con un array a medio inicializar).
 */
bool repo_info_set_languages(RepoInfo *info, size_t count);

#endif /* MODELS_H */
