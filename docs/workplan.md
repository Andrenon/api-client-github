# Plan de Trabajo — api-client-github

**Materia:** Redes de Computadoras II
**Proyecto:** Cliente CLI en C para consumo de la API REST de GitHub, con persistencia en SQLite.

---

## Enfoque general

Estrategia de desarrollo en dos etapas, tal como se define en el README:

```
Fase de diseño → Prototipo funcional en Python → Validación → Implementación final en C → Testing → Entrega
```

El prototipo en Python no es un entregable final: su único objetivo es validar el flujo de consultas HTTP, el modelo de consolidación JSON y el esquema de persistencia antes de invertir esfuerzo en la implementación en C (donde los errores de diseño son más costosos de corregir por la gestión manual de memoria).

---

## Resumen de milestones

| # | Milestone | Fecha estimada |
|---|---|---|
| 0 | Diseño aprobado | 29/06/2026 |
| 1 | Prototipo Python operativo | 21/07/2026 |
| 2 | Prototipo validado | 21/07/2026 |
| 3 | Implementación en C funcional | 27/07/2026 |
| 4 | Criterios de éxito verificados | 27/07/2026 |
| 5 | Entrega final | 28/07/2026 |

---

## Fase 0 — Definición y diseño inicial

**Fecha estimada:** Completado a la fecha (14/07/2026)
**Objetivo:** Cerrar el contrato de software y la arquitectura antes de escribir código.

| Artefacto | Descripción |
|---|---|
| Contrato de software | Ya cubierto por `README.md` — no se genera un documento adicional |
| Documento de arquitectura | Diagrama de componentes + diagrama de secuencia de la "consulta compuesta" (5 llamadas a la API) |

**Milestone:** Diseño aprobado, listo para iniciar el prototipo.

---

## Fase 1 — Prototipo funcional en Python

**Fecha estimada:** 21/07/2026

### Sprint 1.1 — Cliente HTTP básico
| Artefacto | Descripción |
|---|---|
| `http_client.py` | Función GET genérica hacia `api.github.com` con headers (`User-Agent`, `Accept`, `Authorization`). Incluye la consulta al endpoint principal (`GET /repos/{owner}/{repo}`) y el manejo de códigos 401/403/404/429 |

### Sprint 1.2 — Endpoints complementarios
| Artefacto | Descripción |
|---|---|
| Módulos de endpoints | `endpoints/languages.py`, `endpoints/contributors.py`, `endpoints/releases.py`, `endpoints/branches.py` |
| `consolidate.py` | Unifica las 5 respuestas en la estructura JSON definida en el contrato |

### Sprint 1.3 — Persistencia SQLite
| Artefacto | Descripción |
|---|---|
| `schema.sql` | Script de creación de la tabla `assets` |
| `db.py` | Inserción/actualización (upsert) de metadatos por `asset_uri` |

### Sprint 1.4 — CLI y rate limiting
| Artefacto | Descripción |
|---|---|
| `github_client.py` | Punto de entrada CLI (`python github_client.py <owner>/<repo>`); integra los módulos anteriores. Incluye lectura de headers `X-RateLimit-*` e informe al usuario cuando se agota el límite |

**Milestone:** Prototipo Python end-to-end (consulta → consolidación → persistencia) operativo.

---

## Fase 2 — Validación del prototipo

**Fecha estimada:** 21/07/2026

| Artefacto | Descripción |
|---|---|
| Pruebas manuales del prototipo | Casos: repo válido, repo inexistente (404), sin token (rate limit), token inválido (401). Resultados, bugs encontrados y posibles ajustes de diseño documentado en el mismo artefacto |

**Milestone:** Lógica validada y congelada — sirve como especificación funcional para la versión en C.

---

## Fase 3 — Implementación en C

**Fecha estimada:** 27/07/2026

### Sprint 3.1 — Setup del proyecto en C
> Las dependencias (libcurl, cJSON/Jansson, SQLite3) ya están confirmadas en el README (sección Tecnologías); este sprint no las vuelve a definir, solo las deja instaladas/vendorizadas.

| Artefacto | Descripción |
|---|---|
| Estructura `src/`, `include/`, `build/` | Organización del código fuente |
| `Makefile` | Targets de build, clean, y linkeo de libcurl/sqlite3/cJSON |
| `models.h` | Structs compartidos: `RepoInfo`, `Language`, `Contributor`, `Release`, `Branch` |

### Sprint 3.2 — Cliente HTTP con libcurl
| Artefacto | Descripción |
|---|---|
| `http_client.c/h` | Función GET genérica reutilizable para los 5 endpoints. Réplica en C del manejo robusto de errores HTTP y rate limiting validado en el prototipo |

### Sprint 3.3 — Parsing JSON
| Artefacto | Descripción |
|---|---|
| `json_parser.c/h` | Extracción de campos relevantes de cada respuesta usando cJSON |

### Sprint 3.4 — Persistencia SQLite
| Artefacto | Descripción |
|---|---|
| `db.c/h` | Creación de esquema e inserción/actualización de metadatos |

### Sprint 3.5 — Consolidación y lógica principal
| Artefacto | Descripción |
|---|---|
| `core.c/h` | Orquestación de las 5 llamadas y armado del JSON consolidado |

### Sprint 3.6 — CLI
| Artefacto | Descripción |
|---|---|
| `main.c` | Parseo de argumentos (`<owner>/<repo>`) y ensamblado del flujo completo invocando a `core` |

**Milestone:** Binario en C funcional, equivalente al prototipo Python.

---

## Fase 4 — Testing e integración

**Fecha estimada:** 27/07/2026

| Artefacto | Descripción |
|---|---|
| Suite de pruebas | Pruebas unitarias/manuales scriptadas por módulo (parsing, persistencia, HTTP) |
| Checklist de criterios de éxito | Verificación punto por punto contra los "Criterios de Éxito" del README |

**Milestone:** Aplicación cumple todos los criterios de éxito definidos.

---

## Fase 5 — Documentación y entrega final

**Fecha estimada:** 28/07/2026

| Artefacto | Descripción |
|---|---|
| README actualizado | Instalación, compilación, ejemplos de uso, README actualizado |
| Documentación técnica final | Arquitectura definitiva, diagramas actualizados, decisiones de diseño |

**Milestone:** Entrega final del trabajo práctico.

---

