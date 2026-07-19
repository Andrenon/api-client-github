"""
github_client.py

Punto de entrada CLI del prototipo.

    python github_client.py <owner>/<repo> [--json]

Integra los módulos de los sprints anteriores:
    1. consolidate.consolidate()      -> dispara las 5 llamadas y arma el JSON.
    2. db.upsert_asset()              -> persiste el resultado en SQLite.
    3. http_client.get_rate_limit_status() -> informa el estado del rate
       limit al usuario (tanto si la corrida fue exitosa como si no).

Convención de salida: los mensajes informativos/de estado van a stderr; el
JSON crudo (con --json) va a stdout. Así el resultado se puede "pipear" a
otra herramienta (ej. `python github_client.py torvalds/linux --json | jq`)
sin que los mensajes de estado contaminen la salida.
"""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime

import consolidate
import db
import http_client
from http_client import (
    ForbiddenError,
    GitHubAPIError,
    InvalidTokenError,
    RateLimitExceededError,
    RepositoryNotFoundError,
)


def _info(message: str) -> None:
    """Mensaje de estado/progreso -> stderr (no contamina la salida --json)."""
    print(message, file=sys.stderr)


def parse_args(argv: list[str]) -> tuple[str, str, bool]:
    args = argv[1:]
    if len(args) not in (1, 2) or "/" not in args[0] or args[0].count("/") != 1:
        _info("Uso: python github_client.py <owner>/<repo> [--json]")
        _info("Ejemplo: python github_client.py torvalds/linux")
        sys.exit(1)

    owner, repo = args[0].split("/", 1)
    if not owner or not repo:
        _info("Formato inválido. Usá: <owner>/<repo>")
        sys.exit(1)

    as_json = len(args) == 2 and args[1] == "--json"
    if len(args) == 2 and not as_json:
        _info(f"Opción desconocida: {args[1]}")
        sys.exit(1)

    return owner, repo, as_json


def _format_reset(reset_at) -> str | None:
    """Convierte un epoch de X-RateLimit-Reset en una hora legible local."""
    if not reset_at:
        return None
    try:
        reset_dt = datetime.fromtimestamp(int(reset_at))
    except (TypeError, ValueError):
        return None
    return reset_dt.strftime("%H:%M:%S")


def report_rate_limit(token: str | None) -> None:
    """
    Informa proactivamente cuánto presupuesto de rate limit queda. No es
    estrictamente parte del contrato (que solo pide avisar cuando SE AGOTA),
    pero avisar antes de que pase mejora bastante la experiencia de uso.
    Si la consulta a /rate_limit falla por lo que sea, se ignora en
    silencio: no vale la pena frenar el flujo principal por esto.
    """
    try:
        status = http_client.get_rate_limit_status(token=token)
    except GitHubAPIError:
        return

    remaining, limit = status["remaining"], status["limit"]
    _info(f"[rate limit] {remaining}/{limit} solicitudes restantes esta hora.")
    if remaining <= 5:
        reset_msg = _format_reset(status.get("reset"))
        suffix = f" (se reinicia a las {reset_msg})" if reset_msg else ""
        _info(f"[rate limit] Quedan pocas solicitudes disponibles{suffix}.")
        if not token:
            _info(
                "[rate limit] Sugerencia: definí GITHUB_TOKEN para subir "
                "el límite de 60 a 5000 solicitudes/hora."
            )


def main() -> None:
    owner, repo, as_json = parse_args(sys.argv)
    token = os.environ.get("GITHUB_TOKEN")

    _info(f"Consultando github://{owner}/{repo} ...")

    try:
        consolidated = consolidate.consolidate(owner, repo, token=token)
    except RepositoryNotFoundError:
        _info(f"Error: el repositorio '{owner}/{repo}' no existe o no es accesible (404).")
        sys.exit(1)
    except InvalidTokenError:
        _info("Error: el token provisto (GITHUB_TOKEN) es inválido o expiró (401).")
        sys.exit(1)
    except RateLimitExceededError as e:
        reset_msg = _format_reset(e.reset_at)
        _info(f"Error: límite de solicitudes excedido ({e.status_code}).")
        if reset_msg:
            _info(f"El límite se reinicia a las {reset_msg}.")
        if not token:
            _info(
                "Sugerencia: definí GITHUB_TOKEN para subir el límite "
                "de 60 a 5000 solicitudes/hora."
            )
        sys.exit(1)
    except ForbiddenError:
        _info("Error: acceso prohibido (403). Verificá los permisos del token.")
        sys.exit(1)
    except GitHubAPIError as e:
        _info(f"Error inesperado [{e.status_code}]: {e}")
        sys.exit(1)

    conn = db.get_connection()
    db.upsert_asset(conn, owner, repo, consolidated)
    _info(f"Persistido en SQLite como github://{owner}/{repo}")

    if as_json:
        print(json.dumps(consolidated, indent=2, ensure_ascii=False))
    else:
        print(f"{consolidated['name']} ({consolidated['owner']})")
        print(f"  descripción: {consolidated['description'] or '-'}")
        print(
            f"  stars: {consolidated['stars']} | forks: {consolidated['forks']} "
            f"| watchers: {consolidated['watchers']} | rama principal: {consolidated['default_branch']}"
        )
        langs = ", ".join(consolidated["languages"].keys()) or "-"
        print(f"  lenguajes: {langs}")
        contributors_display = (
            consolidated["contributors_count"]
            if consolidated["contributors_count"] is not None
            else "N/D (repo con demasiado historial para que GitHub lo calcule vía API)"
        )
        print(
            f"  contributors: {contributors_display} "
            f"| branches: {consolidated['branches_count']} "
            f"| releases: {consolidated['releases_count']}"
        )

    report_rate_limit(token)


if __name__ == "__main__":
    main()

