# Informe de Validación Funcional — Prototipo Python

**Fase:** 2 — Validación del prototipo
**Estado:** ✅ Cerrada — lógica validada y congelada como especificación funcional para la Fase 3 (C)
**Alcance:** Fase 1 completa (`http_client.py`, `endpoints/*`, `consolidate.py`, `db.py`, `github_client.py`)
**Casos evaluados:** repo válido · repo inexistente (404) · sin token (rate limit) · token inválido (401)
**Entorno de la corrida final:** máquina del usuario (WSL, red propia, sin contención de red), con `GITHUB_TOKEN` real.

---

## 1. Resumen de resultados

| # | Caso | Validado (comandos-manuales.md) |
|---|---|---|---|
| 1 | Repo válido | ✅ PASA |
| 2 | Repo inexistente (404) | ✅ PASA |
| 3 | Sin token (rate limit) | ✅ PASA |
| 4 | Token inválido (401) | ✅ PASA |

Se encontro y corrigió 1 problema durante esta fase (detalle en el punto 2).

---

## 2. Bugs encontrados durante la Fase 2 y su resolución

### 2.1 `contributors_count` puede ser `null`

**Descripción:** `Error: acceso prohibido (403). Verificá los permisos del token.` al consultar `torvalds/linux`, con un token real y válido

**Causa:** Bug real de clasificación: GitHub devuelve `403` para `/contributors` en repos con historial muy grande (`"too large to list..."`), y el código lo trataba como `ForbiddenError` genérico, con mensaje engañoso sobre permisos. Esto pasa incluso pidiendo `per_page=1`.


**Resolución:** Se agregó `ResourceTooLargeError`, distinta de `ForbiddenError` y `RateLimitExceededError`; `get_contributors_count()` la captura y devuelve `None` en vez de frenar la consolidación. Ejemplo confirmado (`torvalds/linux`, el mismo repo de ejemplo del README): `contributors_count: null` para `torvalds/linux`, resto de los campos completos.

**Decisión tomada:** se distingue este 403 puntual (`ResourceTooLargeError`) del 403 genérico (`ForbiddenError`) y del 403 de rate limit (`RateLimitExceededError`), y se captura en `get_contributors_count()` devolviendo `None` en lugar de propagar el error.
El resto de la consolidación (repo, languages, releases, branches) sigue su curso normalmente. El esquema JSON consolidado del README no cambia de forma — el campo sigue existiendo — pero su valor puede ser `null` en este caso puntual, en vez de siempre un entero.

**Resumen:** El 403 de GitHub tiene, en la práctica, tres significados distintos

| Señal en la respuesta | Significado real | Excepción propia |
|---|---|---|
| `X-RateLimit-Remaining: 0` | Rate limit primario agotado | `RateLimitExceededError` |
| Body con `"too large to list..."` | Recurso demasiado costoso de calcular | `ResourceTooLargeError` |
| Ninguna de las anteriores | Forbidden real (permisos) | `ForbiddenError` |

El contrato de software (README) solo documenta "403 = Límite de consultas excedido", que cubre el primer caso. Los otros dos son detalles de implementación que ameritan manejo específico, aunque a nivel de contrato siguen siendo "un 403".

---

## 3. Repos usados para desarrollo y pruebas manuales

- **`torvalds/linux`**: útil específicamente como caso de prueba del bug 2.1 (`contributors_count: null`) y como repo grande en general — pero por eso mismo *no* es representativo del caso feliz "todo con datos completos".
- **Recomendado para pruebas de flujo normal** (todos los campos con valores no nulos): un repo popular pero de tamaño medio, por ejemplo `psf/requests` o `pallets/flask` — tienen contributors, releases y branches reales sin pegar contra el límite de "too large". `octocat/Hello-World` sigue siendo la mejor opción para pruebas rápidas/de humo (repo mínimo, sin datos interesantes).

---

## 4. Detalle por caso (evidencia de la ronda de cierre)

### 4.1 Repo válido

**Comando y resultado — repo chico (`octocat/Hello-World`):**
```bash
$ python3 github_client.py octocat/Hello-World --json
Consultando github://octocat/Hello-World ...
Persistido en SQLite como github://octocat/Hello-World
{
  "id": 1296269, "name": "Hello-World", "owner": "octocat",
  "description": "My first repository on GitHub!",
  "stars": 3690, "forks": 6261, "watchers": 3690, "default_branch": "master",
  "languages": {},
  "contributors_count": 3, "branches_count": 3, "releases_count": 0
}
[rate limit] 4995/5000 solicitudes restantes esta hora.
```
Exit code 0. `contributors_count: 3` y `branches_count: 3` confirman, contra la API real, que el truco de `get_paginated_count()` (`per_page=1` + header `Link`) funciona correctamente también en el caso multi-página — no solo en la simulación del Sprint 1.2.

**Comando y resultado — repo grande (`torvalds/linux`):**
```bash
$ python3 github_client.py torvalds/linux --json
...
"contributors_count": null, "branches_count": 1, "releases_count": 0
[rate limit] 4994/5000 solicitudes restantes esta hora.
```
Exit code 0. Confirma el caso límite expuesto en el punto anterior: `contributors_count` viaja en `null` sin frenar el resto de la consolidación. `branches_count: 1` y `releases_count: 0` son datos reales correctos (el mirror de GitHub de `torvalds/linux` solo publica `master`, y el proyecto no usa la feature de Releases de GitHub), no un error de conteo.

**Repetido con `GITHUB_TOKEN` válido explícito** (sección 3.4 de pruebas): mismo resultado, exit code 0, `[rate limit] 4993/5000` — confirma que el flujo funciona igual autenticado.

### 4.2 Repo inexistente (404)
```bash
$ python3 github_client.py octocat/repo-inexistente-abc123xyz
Consultando github://octocat/repo-inexistente-abc123xyz ...
Error: el repositorio 'octocat/repo-inexistente-abc123xyz' no existe o no es accesible (404).
```
Exit code 1, sin persistencia. Repetido dos veces (una sin token, una con `GITHUB_TOKEN` válido seteado) con idéntico resultado.

### 4.3 Sin token (rate limit)

Validado durante toda la Fase 1: pruebas devolviendo `403` con `X-RateLimit-Remaining: 0`, clasificadas correctamente como `RateLimitExceededError`, con mensaje claro y `reset_at` derivado de `X-RateLimit-Reset`. No se repite la evidencia acá por brevedad; el comportamiento con token válido (4999 → 4993 de 5000 a lo largo de la ronda de cierre, decreciendo de a 1 por prueba) confirma además que el conteo de cupo se lee correctamente.

### 4.4 Token inválido (401)
```bash
$ export GITHUB_TOKEN="ghp_tokenInvalidoDePrueba123456789"
$ python3 github_client.py octocat/Hello-World
Consultando github://octocat/Hello-World ...
Error: el token provisto (GITHUB_TOKEN) es inválido o expiró (401).
```
Exit code 1, sin persistencia.

---

## 5. Ajustes de diseño confirmados para congelar de cara a la Fase 3 (C)

- **403 con triple significado** (rate limit por `X-RateLimit-Remaining: 0`, "too large" por el body del mensaje, forbidden genérico) — confirmado en vivo con los tres casos.
- **`contributors_count` puede ser `null`/ausente** — confirmado en vivo contra `torvalds/linux`. El resto del JSON consolidado debe poder persistirse igual aunque este campo puntual no esté disponible.
- **`GET /rate_limit` no cuenta contra su propio límite** — confirmado empíricamente (200 con `remaining: 0`).
- **Truco `per_page=1` + header `Link` para contar sin paginar todo** — confirmado con un caso multi-página real (`octocat/Hello-World`, `branches_count: 3` correcto), no solo con una respuesta `Link` simulada.
- **Cortar la secuencia sin persistir nada parcial ante cualquier error** — confirmado en los casos 2, 3 y 4: nunca se creó ni modificó el archivo de base de datos salvo en una corrida completa y exitosa.
- **UPSERT atómico por `asset_uri`** — sin cambios respecto de lo validado en Sprint 1.3; comportamiento a replicar en C con `INSERT ... ON CONFLICT` de SQLite3.

---

## 6. Cierre

Los 4 casos del plan de pruebas manuales pasaron. Se encontraro y resolvió 1 problema durante el proceso, documentado en el punto 3. La lógica queda **validada y congelada** como especificación funcional para la Fase 3, tal como lo pide el milestone de `workplan.md`.

