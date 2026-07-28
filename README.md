# api-client-github

## Descripción

`api-client-github` es una aplicación CLI para Linux desarrollada en lenguaje C cuyo objetivo es consumir la API REST de GitHub para extraer metadatos de repositorios públicos y almacenarlos localmente en una base de datos SQLite.

El proyecto forma parte del trabajo final de la materia **Redes de Computadoras II** y se enfoca en la implementación de un cliente HTTP capaz de interactuar con servicios REST sobre HTTPS.

---

# Contrato de Software

## Entrada

El usuario deberá especificar un repositorio utilizando el formato:

```bash
build/github-client <owner>/<repository>
```

Ejemplo:

```bash
build/github-client torvalds/linux
```

---

# Instrucciones rápidas
## Instalación

El proyecto es Linux-only (probado en Ubuntu 24.04). Dependencias de sistema:

```bash
sudo apt-get update
sudo apt-get install libcurl4-openssl-dev libsqlite3-dev libcjson-dev
```

Para confirmar que las tres quedaron resueltas vía `pkg-config` antes de compilar:

```bash
make check-deps
```

## Compilación

```bash
make build      # equivalente a "make"; genera build/github-client
make clean      # borra build/
```

El binario resultante es autocontenido: no depende de ningún archivo en runtime más allá
de la base SQLite que él mismo crea.

## Uso

```bash
build/github-client <owner>/<repo> [--json]
build/github-client octocat/Hello-World --json
```

**Autenticación (opcional):** definir `GITHUB_TOKEN` en el entorno sube el límite de
solicitudes de 60 a 5000 por hora:

```bash
export GITHUB_TOKEN="github_token"
```

## Persistencia

Cada corrida exitosa persiste (o actualiza) un registro en `github_client.db` (SQLite,
creada en el directorio desde donde se ejecuta el binario):

```bash
sqlite3 github_client.db "SELECT asset_uri, title, entity, provider, created_at, updated_at FROM assets;"
sqlite3 github_client.db ".schema assets"
```

## Testing

| Target | Qué corre | Red requerida |
|---|---|---|
| `make smoke-test` | Confirma libcurl/sqlite3/cJSON instalados y `models.h` (15 checks) | No |
| `make test-unit` | `http_client` + `json_parser` + `db` + `core` + `main`, offline (157 checks) | No |
| `make test-live` | Integración de `http_client` contra la API real | Sí |
| `make test-core-live` | Smoke test en vivo de `core_consolidate()` real | Sí |
| `make check-deps` | Verifica que libcurl/sqlite3/libcjson resuelven vía `pkg-config` | No |

---

# Objetivos

* Implementar un cliente HTTP en lenguaje C.
* Consumir servicios REST utilizando HTTPS.
* Comprender el intercambio de mensajes HTTP entre cliente y servidor.
* Procesar respuestas JSON obtenidas desde la API de GitHub.
* Consolidar información proveniente de múltiples endpoints.
* Persistir metadatos de repositorios en una base de datos SQLite.
* Aplicar conceptos de protocolos de aplicación, serialización y comunicación cliente-servidor.

**Estado: cumplido**

---

# Alcance

La herramienta permitirá:

* Consultar repositorios públicos de GitHub.
* Obtener metadatos generales del repositorio.
* Obtener información complementaria desde múltiples endpoints REST.
* Consolidar la información obtenida en una estructura JSON unificada.
* Almacenar localmente los metadatos extraídos.

No se contempla:

* Modificación de repositorios.
* Creación o eliminación de recursos en GitHub.
* Acceso a repositorios privados.
* Operaciones de escritura sobre la API.
* Sincronización automática.
* Registro histórico de cambios de los repositorios.
* Versionado temporal de metadatos.

---

# Arquitectura

```text
+------------------+
| Usuario          |
+--------+---------+
         |
         v
+------------------+
| CLI Linux (main) |
+--------+---------+
         |
         v
+------------------+
| Core/Orquestador |
+--------+---------+
         |
         v
+-----------------------+
| HTTP Client (libcurl) |
+--------+--------------+
         |
 HTTPS / REST
         |
         v
+------------------+
| GitHub API       |
+------------------+
         |
 JSON Parser (cJSON)
         |
         v
+-----------------------+
| Persistencia (SQLite) |
+-----------------------+
```
Mismo diagrama de componentes para Python y C — lo que cambia entre fases es la
tecnología detrás de cada caja, no la división de responsabilidades.

---

# Protocolo de Comunicación

La comunicación se realizará mediante:

* Protocolo HTTP/1.1 o HTTP/2.
* Transporte seguro mediante TLS.
* Conexiones HTTPS hacia la API pública de GitHub.
* Intercambio de información utilizando JSON.

URL base:

```text
https://api.github.com
```

---

# Cabeceras HTTP

Todas las solicitudes deberán incluir como mínimo:

```http
GET /repos/torvalds/linux HTTP/1.1
Host: api.github.com
User-Agent: api-client-github
Accept: application/vnd.github+json
```

Cuando se utilice autenticación:

```http
Authorization: Bearer <TOKEN>
```

---

# API REST Utilizada

## Endpoint principal

Obtención de información general del repositorio.

```http
GET /repos/{owner}/{repo}
```

Ejemplo:

```http
GET https://api.github.com/repos/torvalds/linux
```

Metadatos obtenidos:

* Nombre.
* Descripción.
* Cantidad de estrellas.
* Forks.
* Watchers.
* Rama principal.
* Fecha de creación.
* Fecha de actualización.

---

## Endpoints complementarios

| Endpoint | Información obtenida |
|---|---|
| `GET /repos/{owner}/{repo}/languages` | Lenguajes utilizados y distribución por bytes |
| `GET /repos/{owner}/{repo}/contributors` | Cantidad de contribuidores (puede ser `null`, ver "Uso") |
| `GET /repos/{owner}/{repo}/releases` | Cantidad de releases publicadas |
| `GET /repos/{owner}/{repo}/branches` | Cantidad de ramas |

---

# Consulta Compuesta

La extracción completa de metadatos se realizará mediante múltiples solicitudes HTTP.

```text
1. GET /repos/{owner}/{repo}
2. GET /repos/{owner}/{repo}/languages
3. GET /repos/{owner}/{repo}/contributors
4. GET /repos/{owner}/{repo}/releases
5. GET /repos/{owner}/{repo}/branches
```

Los resultados serán consolidados en una única estructura JSON. Ante el primer error en
cualquiera de las 5 llamadas, la secuencia se corta ahí mismo y no se persiste un
registro parcial — con una única excepción: si la llamada a `/contributors` devuelve el
403 "too large to list..." (repos con historial muy grande), la secuencia **no** se
corta; `contributors_count` queda simplemente sin dato y se continúa con releases y
branches.

---

# Manejo de Respuestas HTTP

La aplicación deberá procesar los principales códigos de estado:

| Código | Significado                  |
| ------ | ---------------------------- |
| 200    | Consulta exitosa             |
| 401    | Token inválido               |
| 403    | Límite de consultas excedido, recurso demasiado costoso de calcular, o forbidden genérico (ver nota)  |
| 404    | Repositorio inexistente      |
| 429    | Demasiadas solicitudes       |

**Nota sobre 403:** en la práctica GitHub usa el mismo código para tres situaciones
distintas (rate limit agotado, "too large to list" en `/contributors`, y forbidden por
permisos), que el cliente distingue y reporta con mensajes distintos.

---

# Rate Limiting

GitHub impone límites de utilización sobre la API.

Sin autenticación:

```text
60 requests por hora
```

Con token personal:

```text
5000 requests por hora
```

La aplicación informa el estado del rate limit al usuario al final de cada corrida
exitosa, y con detalle (incluyendo hora de reset) cuando el error de una corrida es
justamente por límite excedido.

---

# Persistencia

Los metadatos obtenidos se almacenan en la tabla `assets` de una base SQLite
(`github_client.db` por defecto):

```sql
CREATE TABLE IF NOT EXISTS assets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_uri TEXT UNIQUE NOT NULL,
    title TEXT,
    entity TEXT NOT NULL,
    provider TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    meta_payload TEXT
);
```

Mapeo propuesto:

| Campo        | Valor                  |
| ------------ | ---------------------- |
| asset_uri    | github://owner/repo    |
| title        | Nombre del repositorio |
| entity       | repository             |
| provider     | github                 |
| meta_payload | JSON consolidado       |

---

# Esquema JSON Consolidado

Ejemplo:

```json
{
  "id": 2325298,
  "name": "linux",
  "owner": "torvalds",
  "description": "Linux kernel source tree",
  "stars": 215000,
  "forks": 61000,
  "watchers": 8000,
  "default_branch": "master",
  "languages": {
    "C": 950000000,
    "Assembly": 12000000
  },
  "contributors_count": 500,
  "branches_count": 1200,
  "releases_count": 50
}
```

`description` y `contributors_count` pueden viajar como `null` (repo sin descripción, o
repo con historial demasiado grande para que GitHub calcule contribuidores vía API,
respectivamente) — el resto de los campos siempre está presente.

---

# Estrategia de Desarrollo

Se propone implementar inicialmente un prototipo funcional en Python para validar:

* Solicitudes HTTP.
* Procesamiento JSON.
* Flujo de consultas.
* Persistencia SQLite.

Posteriormente se desarrollará la versión final en C.

```text
Python (prototipo)
      ↓
Validación funcional
      ↓
Implementación final en C
```

---

# Tecnologías

## Prototipo

* Python 3
* requests
* sqlite3

## Implementación Final

* C17
* libcurl
* SQLite3
* cJSON
* Makefile

---

# Resolución de bajo nivel (Capas 3, 4 y 7)

El proyecto no implementa sockets, TCP ni TLS a mano — todo eso lo resuelve libcurl (y,
por debajo, el kernel + OpenSSL) dentro de una única llamada a `curl_easy_perform()`
(`http_client.c`). Igual vale la pena mapear explícitamente qué pasa en cada capa del
**modelo híbrido** (Capa 3 Red / Capa 4 Transporte / Capa 7 Aplicación en sentido amplio,
sin separar Presentación ni Sesión) y en qué archivo del proyecto aparece cada cosa.

```
  Cliente (libcurl)             api.github.com
        │                            │
      ┌─┴────────────────────────────┴─┐     
      │  Capa 4 — Transporte           │
      └─┬────────────────────────────┬─┘     
        │────────────SYN────────────>│
        │<───────────SYN-ACK─────────│
        │────────────ACK────────────>│
      ┌─┴────────────────────────────┴─┐     
      │ TCP establecida (Handshake)    │
      └─┬────────────────────────────┬─┘  
      ┌─┴────────────────────────────┴─┐     
      │ "Capa 7" — Aplicación (TLS)    │
      └─┬────────────────────────────┬─┘ 
        │─────────ClientHello───────>│
        │<─ServerHello + Certificado─│
      ┌─┴────────────────────────────┴─┐     
      │ Sesión cifrada lista           │
      └─┬────────────────────────────┬─┘  
      ┌─┴────────────────────────────┴─┐     
      │ Capa 7 — HTTP over TLS (HTTPS) │
      └─┬────────────────────────────┬─┘
        │─GET /repos/{owner}/{repo}─>│
        │<─────────200 + JSON────────│ 
        │                            │
```

## Capa 3 — Red: resolución de nombres

`api.github.com` se resuelve a una dirección IP antes de poder abrir el socket. Una
precisión que vale la pena tener presente: DNS es, en rigor, su propio protocolo de 
aplicación (corre sobre UDP/TCP puerto 53, con su propio formato de mensaje)
— lo ubicamos en Capa 3 acá porque es la dependencia que Capa 3 necesita resuelta antes
de poder rutear un solo paquete (IP necesita una dirección numérica, no un hostname). La
resolución la hace el *resolver* del sistema operativo (glibc, `getaddrinfo()`), invocado
por libcurl — no hay una sola línea de este proyecto ni de libcurl que implemente DNS.

## Capa 4 — Transporte: conexión TCP

El *three-way handshake* (`SYN` → `SYN-ACK` → `ACK`) lo ejecuta el kernel al recibir el
`connect()` sobre el socket que libcurl abre. Es control puro: ningún paquete de esta
etapa lleva certificado, clave de sesión, ni ningún dato de aplicación — TCP todavía no
sabe (ni le importa) si lo que va a transportar después va a estar cifrado. Cero código
del proyecto participa acá: es 100% kernel.

## Capa 7 — Aplicación (sentido amplio): TLS, HTTP, parseo y persistencia

Todo lo demás cae en Capa 7 dentro del modelo híbrido, porque Presentación y Sesión (donde
OSI separaría el cifrado) quedan fusionadas ahí:

1. **Handshake TLS** (`ClientHello` → `ServerHello` + certificado → ... → `Finished`):
   negociado por OpenSSL a través de libcurl, recién después de que el `ACK` de Capa 4 ya
   dejó la conexión establecida. _El certificado viaja acá._
2. **Intercambio HTTP** (ya sobre la sesión cifrada): la línea de request la arma libcurl
   a partir de `CURLOPT_URL`; los headers (`User-Agent`, `Accept`, `Authorization`) los
   arma explícitamente `build_request_headers()` en `http_client.c`. La respuesta llega al
   código a través de dos callbacks que libcurl invoca — `header_write_cb()` y
   `body_write_cb()`, también en `http_client.c` — ya sin TCP ni TLS de por medio.
3. **Clasificación** del `status_code` (`http_client_classify()`, `http_client.c`) — pura
   interpretación, sin red.
4. **Parseo** del body JSON a `RepoInfo` (`json_parser_parse_repo()`/`parse_languages()`,
   `json_parser.c`).
5. **Persistencia** en SQLite (`db_upsert_asset()`, `db.c`).

**Nota importante:** como `http_client_get()` abre y cierra su propio `CURL*` en cada
llamada (no hay *keep-alive* entre solicitudes), esta secuencia completa —Capas 3, 4 y
7— se repite **hasta 6 veces por corrida**: una por cada uno de los 5 endpoints de la
consulta compuesta, más una última vez para `/rate_limit` al final. El diagrama de
secuencia completo de esas 6 llamadas está en `docs/architecture.md`.

---

# Criterios de Éxito

La aplicación será considerada funcional cuando:

* Establezca conexiones HTTPS con la API de GitHub.
* Realice solicitudes HTTP válidas.
* Obtenga información desde múltiples endpoints REST.
* Procese correctamente respuestas JSON.
* Consolide los resultados en una estructura única.
* Persista los metadatos en SQLite.
* Ejecute completamente desde línea de comandos en Linux.

Los 7 criterios definidos para este proyecto están **cumplidos**, con
comandos manuales y detallado en `tests/c/checklist-criterios-exito.md`.

---

# Documentación adicional

| Documento | Contenido |
|---|---|
| `docs/workplan.md` | Plan de trabajo completo, las 5 fases |
| `docs/architecture.md` | Arquitectura, diagramas, decisiones de diseño |
| `tests/prototype/comandos-manuales.md` | Comandos de prueba manual, acumulados por sprint |
| `tests/prototype/informe_validacion.md` | Informe de validación de la Fase 2 (Python) 
| `tests/c/comandos-manuales.md` | Comandos de prueba manual, acumulados por sprint |
| `tests/c/informe_validacion.md` | Informe de validación de la Fase 4 (C) |
| `tests/c/checklist-criterios-exito.md` | Verificación punto por punto contra los criterios de éxito |

