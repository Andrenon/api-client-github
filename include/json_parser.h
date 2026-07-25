#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "http_client.h" /* GitHubErrorCode, GitHubError */
#include "models.h"       /* RepoInfo */

/*
 * json_parser.h — extraccion de campos hacia el esquema consolidado
 * (Sprint 3.3). Replica el mapeo de consolidate.py del prototipo: SOLO
 * los campos que salen de /repos y /languages.
 * contributors_count/branches_count/releases_count NO se tocan aca -ya
 * los resuelve http_client_get_paginated_count() (Sprint 3.2): son
 * conteos genericos, no requieren extraer campos de dominio.
 *
 * Deliberadamente estas funciones reciben `const char *json_body` (no
 * un HttpResponse*): json_parser.c no sabe nada de HTTP -ni status
 * codes, ni headers-, solo transforma texto JSON en campos de RepoInfo.
 * Quien orquesta (core.c, Sprint 3.5) es quien conecta
 * http_client_get_and_classify() con esto, pasandole response->body.
 * Ventaja practica: los tests de este modulo no necesitan armar un
 * HttpResponse como en http_client.c, alcanza con un string JSON de
 * ejemplo escrito a mano.
 *
 * Manejo de errores, distinto de Python a proposito: consolidate.py
 * hace repo_data["id"], repo_data["owner"]["login"], etc. sin try/except
 * -si faltara un campo, un KeyError sin capturar tira todo el programa
 * con un traceback. En C, leer un campo ausente sin chequear es un
 * puntero NULL sin verificar, y usarlo (ej. item->valuestring) no es una
 * excepcion prolija: es comportamiento indefinido. Por eso cada campo
 * requerido se valida (existe + tipo correcto) antes de tocarlo, y ante
 * cualquier problema se devuelve GH_ERR_INTERNAL con un mensaje que dice
 * que campo especifico fallo, en vez de arriesgar un crash. El resultado
 * practico es el mismo que en Python (la operacion no continua, no se
 * usan datos parciales), solo que por un camino controlado.
 */

/*
 * Parsea el body de GET /repos/{owner}/{repo} y puebla en *info los
 * campos id, name, owner, description, stars, forks, watchers y
 * default_branch (equivalente a las 8 primeras claves de
 * consolidate.py). NO toca languages ni los tres contadores.
 *
 * "description" es la unica excepcion al chequeo estricto de tipos:
 * tanto si la clave no existe como si viene explicitamente en JSON
 * null (ambos casos colapsan a repo_data.get("description") == None en
 * Python), se deja info->description en NULL sin que eso sea un error
 * -es el unico campo del contrato pensado para ser opcional.
 *
 * Si tiene exito, dejar *info sin tocar hasta validar TODOS los campos
 * requeridos: o la funcion puebla *info por completo, o no lo toca en
 * absoluto (nunca lo deja a medio popular ante un error a mitad de
 * camino).
 */
GitHubErrorCode json_parser_parse_repo(const char *json_body, RepoInfo *info,
                                        GitHubError *out_error);

/*
 * Parsea el body de GET /repos/{owner}/{repo}/languages (un objeto
 * plano {"lenguaje": bytes, ...}) y puebla info->languages /
 * info->languages_count via repo_info_set_languages(). Un objeto vacio
 * "{}" es un caso VALIDO (repo sin lenguajes detectados, ver ejemplo de
 * octocat/Hello-World en README.md), no un error.
 */
GitHubErrorCode json_parser_parse_languages(const char *json_body, RepoInfo *info,
                                             GitHubError *out_error);

/*
 * Serializa *info al "Esquema JSON Consolidado" del contrato de software
 * (README.md), incluyendo los tres contadores. Es el sentido inverso de
 * las dos funciones de arriba: mientras esas van de JSON -> RepoInfo,
 * esta va de RepoInfo -> JSON. Se agrega en el Sprint 3.4 porque
 * db_upsert_asset() (db.c) necesita el JSON consolidado ya serializado
 * para guardarlo en la columna meta_payload -y es la contraparte
 * natural de json_parser_parse_repo()/json_parser_parse_languages(), no
 * algo que tenga sentido en http_client.c ni en db.c-. También la usará
 * la CLI (Sprint 3.6) para la salida `--json`.
 *
 * contributors_count viaja como JSON null si esta ausente
 * (OptionalCount.present == false), igual que el None de Python.
 * description viaja como JSON null si es NULL, mismo criterio.
 *
 * pretty controla el formato: false para el compacto que espera
 * meta_payload (igual que json.dumps() sin indent en db.py), true para
 * el formato con indentacion de 2 espacios que usa la CLI --json (igual
 * que json.dumps(..., indent=2) en github_client.py).
 *
 * Devuelve true + *out_json (owned, el caller hace free()), o false +
 * *out_error poblado (solo puede fallar por falta de memoria).
 */
bool json_parser_serialize_repo_info(const RepoInfo *info, bool pretty, char **out_json,
                                      GitHubError *out_error);

#endif /* JSON_PARSER_H */

