"""
endpoints/releases.py

GET /repos/{owner}/{repo}/releases
"""

from typing import Optional

import http_client


def get_releases_count(owner: str, repo: str, token: Optional[str] = None) -> int:
    """Cantidad total de releases publicadas (ver nota en contributors.py)."""
    return http_client.get_paginated_count(
        f"/repos/{owner}/{repo}/releases", token=token
    )

