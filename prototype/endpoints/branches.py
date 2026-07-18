"""
endpoints/branches.py

GET /repos/{owner}/{repo}/branches
"""

from typing import Optional

import http_client


def get_branches_count(owner: str, repo: str, token: Optional[str] = None) -> int:
    """Cantidad total de ramas (ver nota en contributors.py)."""
    return http_client.get_paginated_count(
        f"/repos/{owner}/{repo}/branches", token=token
    )

