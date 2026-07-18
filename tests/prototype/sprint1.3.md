**Prueba manual**

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

