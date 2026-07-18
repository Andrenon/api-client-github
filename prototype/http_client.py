"""
http_client.py

Cliente HTTP genérico para consumir la API REST de GitHub (api.github.com).

Responsabilidades (según docs/architecture.md, componente "HTTP Client"):
- Agregar los headers obligatorios (User-Agent, Accept, Authorization opcional).
- Ejecutar solicitudes GET con timeout.
- Interpretar los códigos de estado 200/401/403/404/429 y traducirlos a
  excepciones tipadas, para que las capas superiores (Core, en Sprint 1.4/1.2)
  puedan reaccionar sin volver a inspeccionar el status code.
"""

from __future__ import annotations

import os
from typing import Any, Optional
from urllib.parse import parse_qs, urlparse

import requests

GITHUB_API_BASE_URL = "https://api.github.com"
USER_AGENT = "api-client-github"
DEFAULT_TIMEOUT = 10  # segundos


# ---------------------------------------------------------------------------
# Excepciones
# ---------------------------------------------------------------------------

class GitHubAPIError(Exception):
    """Excepción base para cualquier error al consultar la API de GitHub."""

    def __init__(
        self,
        message: str,
        status_code: Optional[int] = None,
        response: Optional[requests.Response] = None,
    ) -> None:
        super().__init__(message)
        self.status_code = status_code
        self.response = response


class InvalidTokenError(GitHubAPIError):
    """401 - Token inválido o expirado."""


class ForbiddenError(GitHubAPIError):
    """403 - Acceso prohibido (no relacionado a rate limit)."""


class RepositoryNotFoundError(GitHubAPIError):
    """404 - El repositorio no existe, o no es accesible sin autenticación."""


class RateLimitExceededError(GitHubAPIError):
    """
    429, o 403 con X-RateLimit-Remaining: 0 - Límite de tasa excedido.

    GitHub reporta el agotamiento del rate limit primario con 403 (no 429)
    cuando el header X-RateLimit-Remaining llega a 0; 429 se usa para límites
    secundarios (ráfagas de requests). Se contemplan ambos casos.
    """

    def __init__(
        self,
        message: str,
        status_code: Optional[int] = None,
        response: Optional[requests.Response] = None,
        reset_at: Optional[str] = None,
    ) -> None:
        super().__init__(message, status_code, response)
        self.reset_at = reset_at


# ---------------------------------------------------------------------------
# Cliente HTTP
# ---------------------------------------------------------------------------

def _build_headers(token: Optional[str]) -> dict[str, str]:
    headers = {
        "User-Agent": USER_AGENT,
        "Accept": "application/vnd.github+json",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def get(
    path: str,
    token: Optional[str] = None,
    params: Optional[dict[str, Any]] = None,
    timeout: int = DEFAULT_TIMEOUT,
) -> requests.Response:
    """
    Ejecuta un GET genérico contra la API de GitHub.

    Args:
        path: path relativo a GITHUB_API_BASE_URL, ej "/repos/torvalds/linux".
        token: token personal opcional. Si no se pasa, se intenta leer la
            variable de entorno GITHUB_TOKEN (para no hardcodear secretos).
        params: query params opcionales.
        timeout: timeout de la request en segundos.

    Returns:
        El objeto requests.Response completo (no solo el JSON) para que el
        caller pueda leer headers como X-RateLimit-* (necesario en Sprint 1.4).

    Raises:
        InvalidTokenError, ForbiddenError, RepositoryNotFoundError,
        RateLimitExceededError, GitHubAPIError.
    """
    token = token or os.environ.get("GITHUB_TOKEN")
    url = f"{GITHUB_API_BASE_URL}{path}"
    headers = _build_headers(token)

    try:
        response = requests.get(url, headers=headers, params=params, timeout=timeout)
    except requests.exceptions.RequestException as exc:
        raise GitHubAPIError(f"Error de red al consultar {url}: {exc}") from exc

    _raise_for_status(response)
    return response


def _raise_for_status(response: requests.Response) -> None:
    status = response.status_code

    if status == 200:
        return

    if status == 401:
        raise InvalidTokenError(
            "Token inválido o expirado (401).", status_code=401, response=response
        )

    if status == 404:
        raise RepositoryNotFoundError(
            "Repositorio inexistente o no accesible (404).",
            status_code=404,
            response=response,
        )

    if status == 429:
        raise RateLimitExceededError(
            "Demasiadas solicitudes, límite de tasa excedido (429).",
            status_code=429,
            response=response,
            reset_at=response.headers.get("X-RateLimit-Reset"),
        )

    if status == 403:
        remaining = response.headers.get("X-RateLimit-Remaining")
        if remaining == "0":
            raise RateLimitExceededError(
                "Límite de tasa excedido (403 con X-RateLimit-Remaining=0).",
                status_code=403,
                response=response,
                reset_at=response.headers.get("X-RateLimit-Reset"),
            )
        raise ForbiddenError(
            "Acceso prohibido (403).", status_code=403, response=response
        )

    # Código no contemplado explícitamente en el contrato de software.
    raise GitHubAPIError(
        f"Respuesta inesperada de la API: {status}",
        status_code=status,
        response=response,
    )


def get_paginated_count(path: str, token: Optional[str] = None) -> int:
    """
    Cuenta la cantidad total de elementos de un endpoint paginado sin traer
    todas las páginas.

    Estrategia: se pide la página con per_page=1. GitHub agrega un header
    `Link` con rel="last" que contiene el número de la última página — como
    cada página tiene 1 elemento, ese número de página *es* el total. Si no
    hay rel="last" (porque hay 0 o 1 elementos en total), se cuenta directo
    del body.

    Esto evita traer el listado completo (que en repos grandes puede ser
    cientos de páginas) solo para saber cuántos elementos hay, ahorrando
    presupuesto de rate limit.
    """
    response = get(path, token=token, params={"per_page": 1})
    if "last" in response.links:
        last_url = response.links["last"]["url"]
        query = parse_qs(urlparse(last_url).query)
        return int(query["page"][0])
    return len(response.json())


# ---------------------------------------------------------------------------
# Uso manual rápido: prueba el cliente HTTP crudo con cualquier path de la API
# (para probar un endpoint puntual sin pasar por los módulos de endpoints/)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import json
    import sys

    if len(sys.argv) != 2 or not sys.argv[1].startswith("/"):
        print("Uso: python http_client.py /repos/<owner>/<repo>")
        print("Ejemplo: python http_client.py /repos/torvalds/linux")
        sys.exit(1)

    try:
        resp = get(sys.argv[1])
        print(json.dumps(resp.json(), indent=2, ensure_ascii=False))
    except GitHubAPIError as e:
        print(f"Error [{e.status_code}]: {e}")
        sys.exit(1)

