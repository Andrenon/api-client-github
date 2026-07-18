"""
db.py

Persistencia SQLite (según docs/architecture.md, componente "Persistencia"):
- Crea el esquema `assets` si no existe.
- Hace upsert de los metadatos consolidados de un repo, indexado por
  `asset_uri` (formato: github://{owner}/{repo}).
"""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any, Optional

DB_PATH = Path(__file__).parent / "github_client.db"
SCHEMA_PATH = Path(__file__).parent / "schema.sql"


def get_connection(db_path: Path | str = DB_PATH) -> sqlite3.Connection:
    """
    Abre una conexión a la base SQLite y garantiza que el esquema `assets`
    ya exista antes de devolverla, para que el resto del código nunca tenga
    que acordarse de inicializar el schema por separado.
    """
    conn = sqlite3.connect(db_path)
    _init_schema(conn)
    return conn


def _init_schema(conn: sqlite3.Connection) -> None:
    with open(SCHEMA_PATH, "r", encoding="utf-8") as f:
        conn.executescript(f.read())
    conn.commit()


def upsert_asset(
    conn: sqlite3.Connection,
    owner: str,
    repo: str,
    meta_payload: dict[str, Any],
) -> None:
    """
    Inserta o actualiza (por asset_uri) el registro consolidado de un repo.

    Mapeo, tal como lo define el README:
        asset_uri    -> github://{owner}/{repo}
        title        -> meta_payload["name"]
        entity       -> "repository"
        provider     -> "github"
        meta_payload -> JSON consolidado (serializado a texto)

    Se usa el UPSERT nativo de SQLite (INSERT ... ON CONFLICT DO UPDATE) en
    vez de un SELECT previo + INSERT/UPDATE manual: es atómico -no hay
    ventana entre "chequear si existe" y "escribir" donde otro proceso
    pueda meterse- y además es menos código.

    `updated_at` se refresca a mano en la cláusula DO UPDATE porque el
    DEFAULT CURRENT_TIMESTAMP de la columna solo se aplica en el INSERT
    inicial, nunca en updates posteriores (comportamiento estándar de
    SQLite). `created_at`, en cambio, no se toca en el UPDATE a propósito:
    tiene que conservar la fecha del primer alta del repo.
    """
    asset_uri = f"github://{owner}/{repo}"
    title = meta_payload.get("name")
    payload_json = json.dumps(meta_payload, ensure_ascii=False)

    conn.execute(
        """
        INSERT INTO assets (asset_uri, title, entity, provider, meta_payload)
        VALUES (:asset_uri, :title, :entity, :provider, :meta_payload)
        ON CONFLICT(asset_uri) DO UPDATE SET
            title = excluded.title,
            meta_payload = excluded.meta_payload,
            updated_at = CURRENT_TIMESTAMP
        """,
        {
            "asset_uri": asset_uri,
            "title": title,
            "entity": "repository",
            "provider": "github",
            "meta_payload": payload_json,
        },
    )
    conn.commit()


def get_asset(conn: sqlite3.Connection, owner: str, repo: str) -> Optional[dict[str, Any]]:
    """
    Recupera un registro por asset_uri, con meta_payload ya deserializado.
    Pensado para validación manual y para los tests de Fase 2.
    """
    asset_uri = f"github://{owner}/{repo}"
    cursor = conn.execute("SELECT * FROM assets WHERE asset_uri = ?", (asset_uri,))
    row = cursor.fetchone()
    if row is None:
        return None

    columns = [col[0] for col in cursor.description]
    result = dict(zip(columns, row))
    result["meta_payload"] = json.loads(result["meta_payload"])
    return result


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2 or "/" not in sys.argv[1]:
        print("Uso: python db.py <owner>/<repo>  (busca el registro persistido)")
        sys.exit(1)

    owner_arg, repo_arg = sys.argv[1].split("/", 1)

    connection = get_connection()
    asset = get_asset(connection, owner_arg, repo_arg)
    if asset is None:
        print(f"No hay ningún registro para github://{owner_arg}/{repo_arg}")
    else:
        print(json.dumps(asset, indent=2, ensure_ascii=False))

