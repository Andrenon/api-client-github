# Comandos manuales — Fase 3 (C)

Todos los comandos se corren desde la raíz del repo (`api-client-github/`).

### Con token, agregar

```bash
-H "Authorization: Bearer $GITHUB_TOKEN" \
```

**Previa carga de la variable**:
```bash
export GITHUB_TOKEN="tu_token"
```

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

### `make build` sigue fallando a propósito (todavía sin `main.c`, hasta 3.6)

```bash
make clean && make build   # esperado: "undefined reference to `main'"
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

