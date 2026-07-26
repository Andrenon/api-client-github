# Comandos manuales — Fase 3 (C)

Todos los comandos se corren desde la raíz del repo (`api-client-github/`).

### Con token agregar:
`-H "Authorization: Bearer $GITHUB_TOKEN" \`

Previo cargar variable de entorno:
`export GITHUB_TOKEN="github_token"`

---

## Sprint 3.1 — Setup del proyecto en C

### Instalar dependencias del sistema (Ubuntu/Debian)

```bash
apt-get install libcurl4-openssl-dev libsqlite3-dev libcjson-dev
```

### Confirmar que quedaron resueltas vía pkg-config

```bash
pkg-config --cflags --libs libcurl
pkg-config --cflags --libs sqlite3
pkg-config --cflags --libs libcjson
```

### Compilar y correr el smoke test (libs + include/models.h)

```bash
make check-deps
make smoke-test
```

### `make build` sin fuentes de negocio (falla a propósito hasta el Sprint 3.6)

```bash
make clean && make build   # esperado: "undefined reference to `main'"
```

### Chequeo de memoria del ciclo de vida de RepoInfo

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/smoke_test
```

---

## Sprint 3.2 — Cliente HTTP con libcurl

### Compilar y correr los unit tests offline (sin red, fixtures a mano)

```bash
make test-unit
```

### Chequeo de memoria de los unit tests

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test_http_client_unit
```

### Tests en vivo contra la API real (sin token, pool de 60 req/hora)

```bash
make test-live
```

### Curl equivalentes, para comparar la respuesta cruda vs. lo que interpreta `http_client.c`

Repo válido:
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux"
```

Repo inexistente (404):
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/octocat/repo-inexistente-github-client-abc123"
```

Token inválido (401):
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer ghp_tokenInvalidoDePrueba123456789" \
  "https://api.github.com/repos/torvalds/linux"
```

403 "too large to list" (caso real, ver `docs/notas-implementacion.md` #1):
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux/contributors?per_page=1"
```

Truco `per_page=1` + header `Link` (repo multi-página real → `rel="last"` presente):
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/octocat/Hello-World/branches?per_page=1"
```

Mismo truco, caso fallback (repo con 0 o 1 página → sin header `Link`):
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux/branches?per_page=1"

curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux/releases?per_page=1"
```

Rate limit crudo (para comparar contra `http_client_get_rate_limit_status()`):
```bash
curl -s -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/rate_limit" | python3 -m json.tool
```

---

## Sprint 3.3 — Parsing JSON con cJSON

### Compilar y correr los unit tests offline (http_client + json_parser)

```bash
make test-unit
```

### Chequeo de memoria

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test_http_client_unit
valgrind --leak-check=full --error-exitcode=1 ./build/test_json_parser_unit
```

### Curl para comparar el JSON crudo contra lo que extrae `json_parser_parse_repo` / `json_parser_parse_languages`

```bash
curl -s -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/octocat/Hello-World" | python3 -m json.tool

curl -s -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/octocat/Hello-World/languages" | python3 -m json.tool
```

---

## Sprint 3.4 — Persistencia SQLite

### Compilar y correr todos los unit tests offline (http_client + json_parser + db)

```bash
make test-unit
```

### Chequeo de memoria específico de db.c

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test_db_unit
```

### Inspeccionar a mano una base creada por el binario en C (una vez exista main.c, Sprint 3.6)

Mismos comandos que `prototype/github_client.db`, aplicados al `github_client.db` que genere el binario en C:

```bash
sqlite3 github_client.db "SELECT asset_uri, title, entity, provider, created_at, updated_at FROM assets;"
sqlite3 github_client.db ".schema assets"
```

---

## Sprint 3.5 — Consolidación y lógica principal (core.c)

### Compilar y correr todos los unit tests offline (incluye core con dependencias inyectadas)

```bash
make test-unit
```

### Chequeo de memoria específico de core.c

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test_core_unit
```

### Smoke test en vivo del wiring real (core_consolidate real, no inyectado)

```bash
make test-core-live
```

---

## Sprint 3.6 — CLI (main.c) — último sprint de la Fase 3

### Compilar el binario final (por primera vez en el proyecto)

```bash
make build
```

### Compilar y correr todos los unit tests offline (los 6 módulos, incluye main.c)

```bash
make test-unit
```

### Casos de parseo de argumentos (sin red)

```bash
./build/github-client                        # sin argumentos
./build/github-client torvalds-linux          # sin '/'
./build/github-client a/b/c                   # dos '/'
./build/github-client /linux                  # owner vacío
./build/github-client torvalds/linux --xml    # opción desconocida
```

### Casos con red — análogos a los comandos de Sprint 1.4 en Python

```bash
./build/github-client torvalds/linux                       # contributors: N/D
./build/github-client pallets/flask
./build/github-client torvalds/linux --json
./build/github-client pallets/flask --json
./build/github-client octocat/repo-que-no-existe-123        # caso 404
GITHUB_TOKEN=token_invalido ./build/github-client torvalds/linux   # caso 401
```

### Inspeccionar la base persistida por el binario real

```bash
sqlite3 github_client.db "SELECT asset_uri, title, entity, provider, created_at, updated_at FROM assets;"
sqlite3 github_client.db ".schema assets"
```

### Chequeo de memoria del binario real (no solo de los tests con fakes)

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/github-client
valgrind --leak-check=full --error-exitcode=1 ./build/github-client torvalds/linux
```

