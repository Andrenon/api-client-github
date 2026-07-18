"""
endpoints/contributors.py

GET /repos/{owner}/{repo}/contributors
"""

from typing import Optional

import http_client


def get_contributors_count(owner: str, repo: str, token: Optional[str] = None) -> int:
    """
    Cantidad total de contribuidores.

    Se usa http_client.get_paginated_count() en lugar de traer el listado
    completo: el esquema JSON consolidado (README) solo persiste el conteo
    (`contributors_count`), no la lista de nombres, así que no vale la pena
    gastar presupuesto de rate limit trayendo todas las páginas de un repo
    grande (ej. torvalds/linux tiene cientos de contribuidores).
    """
    return http_client.get_paginated_count(
        f"/repos/{owner}/{repo}/contributors", token=token
    )

