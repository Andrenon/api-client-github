"""
endpoints/contributors.py

GET /repos/{owner}/{repo}/contributors
"""

from typing import Optional

import http_client


def get_contributors_count(owner: str, repo: str, token: Optional[str] = None) -> Optional[int]:
    """
    Cantidad total de contribuidores, o None si GitHub no puede calcularla.

    Se usa http_client.get_paginated_count() en lugar de traer el listado
    completo: el esquema JSON consolidado (README) solo persiste el conteo
    (`contributors_count`), no la lista de nombres, así que no vale la pena
    gastar presupuesto de rate limit trayendo todas las páginas de un repo
    grande (ej. torvalds/linux tiene cientos de contribuidores).

    Repos con historial muy grande (torvalds/linux, chromium/chromium, y
    otros por el estilo) hacen que GitHub directamente se niegue a calcular
    este listado (403 "too large to list"), sin importar el token ni sus
    permisos. Se captura ese caso puntual y se devuelve None en vez de
    dejar que la excepción tire abajo toda la consolidación: es preferible
    persistir el resto de los metadatos con este campo en null a no poder
    procesar el repo en absoluto.
    """
    try:
        return http_client.get_paginated_count(
            f"/repos/{owner}/{repo}/contributors", token=token
        )
    except http_client.ResourceTooLargeError:
        return None

