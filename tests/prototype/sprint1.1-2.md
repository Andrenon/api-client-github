**Pruebas manuales**

### Sin token:
```bash
curl -i \
  -H "User-Agent: api-client-github" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/torvalds/linux"
```
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

### Con token agregar:
`-H "Authorization: Bearer $GITHUB_TOKEN" \`

Previo cargar variable de entorno:
`export GITHUB_TOKEN="github_token"`

