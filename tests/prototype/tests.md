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
import db
conn = db.get_connection()
db.upsert_asset(conn, 'torvalds', 'linux', {'name': 'linux', 'stars': 215000})
print(db.get_asset(conn, 'torvalds', 'linux'))
"
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



