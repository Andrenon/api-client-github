# Checklist de Criterios de Éxito — Implementación en C

**Fase:** 4 — Testing e integración
**Referencia:** `README.md`, sección "Criterios de Éxito"
**Evidencia de respaldo:** `tests/c/comandos-manuales.md`

El README define 7 criterios.

---

## ☑ Establezca conexiones HTTPS con la API de GitHub

- `http_client.c` usa libcurl con `https://api.github.com` como base
  (`GITHUB_API_BASE_URL`); libcurl en este entorno está compilado con soporte SSL/TLS
  (verificado explícitamente en `smoke_test.c`, Sprint 3.1).
- **Confirmado con comandos manuales a lo largo de la Fase 3**: respuestas reales 200,
  401, 403 y 429 recibidas contra `https://api.github.com`.

**Estado: cumplido**, con evidencia en vivo real (no solo teórica).

---

## ☑ Realice solicitudes HTTP válidas

- Headers obligatorios del contrato (`User-Agent: api-client-github`,
  `Accept: application/vnd.github+json`, `Authorization: Bearer <token>` cuando
  corresponde) armados en `build_request_headers()` (`http_client.c`).
- **Confirmado en vivo**: si los headers estuvieran mal formados, GitHub no habría
  respondido nunca con 200 (lo hizo, ver arriba) ni habría distinguido correctamente
  autenticado vs. no-autenticado (el caso 401 con token inválido solo tiene sentido si el
  header `Authorization` efectivamente viajó).

**Estado: cumplido**

---

## ☑ Obtenga información desde múltiples endpoints REST

- `core_consolidate()` dispara las 5 llamadas del contrato en orden
  (`/repos/{owner}/{repo}`, `.../languages`, `.../contributors`, `.../releases`,
  `.../branches`) — `src/core.c`.
- **Test determinístico** (`test_core_unit.c`, 25 checks): confirma que las 5 llamadas se
  disparan en el orden correcto, y exactamente dónde se corta la secuencia ante cada tipo
  de error.

**Estado: cumplido**

---

## ☑ Procese correctamente respuestas JSON

- `json_parser.c` extrae los campos del contrato desde el JSON crudo de `/repos` y
  `/languages`, con validación explícita de tipo/presencia para cada campo requerido (no
  asume que el shape es siempre el esperado).
- **47 checks** en `test_json_parser_unit.c`: caso feliz, `description` nulo/ausente,
  3 variantes de campo faltante/incorrecto, objeto `languages` vacío, valor no numérico.
- **Confirmado con comandos manuales**: bodies reales de `octocat/Hello-World` y 
  `torvalds/linux` procesados correctamente durante la Fase 3 (ids, nombres, conteos 
  coinciden con `tests/prototype/informe_validacion.md`).

**Estado: cumplido.**

---

## ☑ Consolide los resultados en una estructura única

- `json_parser_serialize_repo_info()` arma exactamente el "Esquema JSON Consolidado" del
  README: `id`, `name`, `owner` (plano), `description`, `stars`/`forks`/`watchers`
  (renombrados desde los campos crudos de la API), `default_branch`, `languages`,
  `contributors_count` (puede ser `null`), `branches_count`, `releases_count`.
- Verificado campo por campo contra valores conocidos en `test_json_parser_unit.c`
  (`test_serialize_roundtrip` y afines), incluyendo los dos casos de `null` explícito
  (`description`, `contributors_count`) y el formato `pretty`/compacto.
- Es la misma estructura que `db.c` persiste (`meta_payload`) y que `main.c` imprime con
  `--json` — un único punto de armado reusado en los dos lugares donde hace falta.

**Estado: cumplido.**

---

## ☑ Persista los metadatos en SQLite

- `db.c` crea la tabla `assets` (DDL embebido, idéntico a `README.md`/`schema.sql`) y hace
  `UPSERT` atómico por `asset_uri`.
- **27 checks** en `test_db_unit.c`: alta, actualización (con `created_at` preservado y
  `updated_at` refrescado, verificado sin depender de que pase más de un segundo real
  entre corridas), no colisión entre repos, binding parametrizado seguro contra
  caracteres especiales, `title` `NULL`.
- **Confirmado con comandos manuales**: persistencia real en disco del binario compilado,
  `github_client.db`.

**Estado: cumplido**

---

## ☑ Ejecute completamente desde línea de comandos en Linux

- `make build` produce `build/github-client`, un binario nativo de Linux.
- `main.c` parsea `<owner>/<repo> [--json]` desde `argv`.
- **Confirmado con comandos manuales**: cubierto en el Sprint 3.6.

**Estado: cumplido**

---

## Resumen

| # | Criterio | Estado |
|---|---|---|---|
| 1 | HTTPS con la API de GitHub | ✅ |
| 2 | Solicitudes HTTP válidas | ✅ |
| 3 | Múltiples endpoints REST | ✅ |
| 4 | Procesa JSON correctamente | ✅ |
| 5 | Estructura consolidada única | ✅ |
| 6 | Persiste en SQLite | ✅ |
| 7 | CLI completo en Linux | ✅ |

**Los 7 criterios del README se cumplen.**

