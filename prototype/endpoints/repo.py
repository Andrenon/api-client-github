"""
endpoints/repo.py

GET /repos/{owner}/{repo}  — endpoint principal del contrato de software.
"""

from typing import Any, Optional

import http_client


def get_repo(owner: str, repo: str, token: Optional[str] = None) -> dict[str, Any]:
    """Devuelve el JSON crudo de GET /repos/{owner}/{repo}."""
    response = http_client.get(f"/repos/{owner}/{repo}", token=token)
    return response.json()

