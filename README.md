# api-client-github

## Descripción

`api-client-github` es una aplicación CLI para Linux desarrollada en lenguaje C cuyo objetivo es consumir la API REST de GitHub para extraer metadatos de repositorios públicos y almacenarlos localmente en una base de datos SQLite.

El proyecto forma parte del trabajo final de la materia **Redes de Computadoras II** y se enfoca en la implementación de un cliente HTTP capaz de interactuar con servicios REST sobre HTTPS.

---

# Objetivos

* Implementar un cliente HTTP en lenguaje C.
* Consumir servicios REST utilizando HTTPS.
* Comprender el intercambio de mensajes HTTP entre cliente y servidor.
* Procesar respuestas JSON obtenidas desde la API de GitHub.
* Consolidar información proveniente de múltiples endpoints.
* Persistir metadatos de repositorios en una base de datos SQLite.
* Aplicar conceptos de protocolos de aplicación, serialización y comunicación cliente-servidor.

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
| CLI Linux        |
+--------+---------+
         |
         v
+------------------+
| API Client (C)   |
+--------+---------+
         |
 HTTPS / REST
         |
         v
+------------------+
| GitHub API       |
+------------------+
         |
 JSON
         |
         v
+------------------+
| SQLite           |
+------------------+
```

---

# Contrato de Software

## Entrada

El usuario deberá especificar un repositorio utilizando el formato:

```bash
github-client <owner>/<repository>
```

Ejemplo:

```bash
github-client torvalds/linux
```

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

### Lenguajes

```http
GET /repos/{owner}/{repo}/languages
```

Información obtenida:

* Lenguajes utilizados.
* Distribución por cantidad de bytes.

---

### Contributors

```http
GET /repos/{owner}/{repo}/contributors
```

Información obtenida:

* Lista de contribuidores.
* Cantidad de contribuciones.

---

### Releases

```http
GET /repos/{owner}/{repo}/releases
```

Información obtenida:

* Releases publicadas.
* Cantidad de versiones disponibles.

---

### Branches

```http
GET /repos/{owner}/{repo}/branches
```

Información obtenida:

* Cantidad de ramas.
* Nombre de las ramas.

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

Los resultados serán consolidados en una única estructura JSON.

---

# Manejo de Respuestas HTTP

La aplicación deberá procesar los principales códigos de estado:

| Código | Significado                  |
| ------ | ---------------------------- |
| 200    | Consulta exitosa             |
| 401    | Token inválido               |
| 403    | Límite de consultas excedido |
| 404    | Repositorio inexistente      |
| 429    | Demasiadas solicitudes       |

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

La aplicación deberá informar al usuario cuando se alcance un límite impuesto por la API.

---

# Persistencia

Los metadatos obtenidos serán almacenados en la tabla:

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
* cJSON o Jansson
* Makefile

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
