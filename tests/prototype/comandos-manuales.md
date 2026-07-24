**Pruebas manuales**

### Con token agregar:
`-H "Authorization: Bearer $GITHUB_TOKEN" \`

Previo cargar variable de entorno:
`export GITHUB_TOKEN="github_token"`

### Sprint 1.1
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux"
```
### Sprint 1.2
Endpoints complementarios (Languages, Contributors, Releases, Branches):
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux/contributors?per_page=1"

curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux/releases?per_page=1"

curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux/branches?per_page=1"
```

### Probar Sprint 1.3
```bash
cd prototype
```
```bash
python3 -c "
import sqlite3, json
conn = sqlite3.connect('github_client.db')
for row in conn.execute('SELECT asset_uri, title, entity, provider, created_at, updated_at FROM assets'):
    print(row)
"
```
Con el CLI de sqlite3:
```bash
# Ver que la tabla exista con el esquema correcto
sqlite3 github_client.db ".schema assets"

# Ver todas las filas persistidas (formato tabla legible)
sqlite3 -header -column github_client.db \
  "SELECT id, asset_uri, title, entity, provider, created_at, updated_at FROM assets;"

# Ver el meta_payload completo de un repo puntual, formateado
sqlite3 github_client.db \
  "SELECT meta_payload FROM assets WHERE asset_uri='github://torvalds/linux';" | python3 -m json.tool

# Confirmar que el upsert no duplica: correlo dos veces seguidas...
python3 github_client.py octocat/Hello-World --json >/dev/null
python3 github_client.py octocat/Hello-World --json >/dev/null
# ...y verificar que sigue habiendo 1 sola fila para ese asset_uri
sqlite3 github_client.db \
  "SELECT COUNT(*) FROM assets WHERE asset_uri='github://octocat/Hello-World';"

# Confirmar que created_at no cambia pero updated_at sí, entre esas dos corridas
sqlite3 -header -column github_client.db \
  "SELECT asset_uri, created_at, updated_at FROM assets WHERE asset_uri='github://octocat/Hello-World';"
```

### Sprint 1.4
```bash
cd prototype
```
```bash
python3 github_client.py torvalds/linux   # contributors: N/D (repo con demasiado historial para que GitHub lo calcule vía API)
python3 github_client.py pallets/flask
python3 github_client.py torvalds/linux --json | python3 -m json.tool
python3 github_client.py pallets/flask --json | python3 -m json.tool
python3 github_client.py octocat/repo-que-no-existe-123   # caso 404
GITHUB_TOKEN=token_invalido python3 github_client.py torvalds/linux   # caso 401
```

Curl para ver el rate limit crudo que consulta `report_rate_limit()`:
```bash
curl -s -H "User-Agent: api-client-github"  \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/rate_limit" | python3 -m json.tool
  
curl -s \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  "https://api.github.com/rate_limit" | python3 -m json.tool
```

