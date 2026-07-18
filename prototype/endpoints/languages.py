"""
endpoints/languages.py

GET /repos/{owner}/{repo}/languages
"""

from typing import Optional

import http_client


def get_languages(owner: str, repo: str, token: Optional[str] = None) -> dict:
    """
    Devuelve el dict {lenguaje: bytes} tal como lo entrega la API.

    A diferencia de contributors/releases/branches, este endpoint no está
    paginado: la respuesta ya es el objeto completo, así que se devuelve
    directo sin transformación (la responsabilidad de mapearlo al esquema
    final es de consolidate.py).
    """
    response = http_client.get(f"/repos/{owner}/{repo}/languages", token=token)
    return response.json()

