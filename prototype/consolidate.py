"""
consolidate.py

Orquesta las 5 llamadas a la API (en el orden definido en el README) y arma
el esquema JSON consolidado.

Nota de arquitectura: en docs/architecture.md el "Core/Orquestador" y el
"JSON Parser/Consolidador" son dos cajas separadas. En el prototipo Python se
fusionan en este único módulo (así lo define workplan.md para la Fase 1, que
no incluye un core.py separado) — la separación real en dos componentes se
concreta recién en la Fase 3 (C), con core.c y json_parser.c.

La detención ante el primer error (401/403/404/429) no requiere try/except
explícito acá: al ser un flujo secuencial imperativo, si cualquiera de las
llamadas de http_client lanza una excepción, las líneas siguientes de
consolidate() simplemente no se ejecutan y la excepción sube tal cual hacia
el caller (github_client.py en Sprint 1.4), que decide cómo reportarla y
evita persistir un registro parcial.
"""

from typing import Any, Optional

from endpoints import branches, contributors, languages, releases
from endpoints import repo as repo_endpoint


def consolidate(owner: str, repo: str, token: Optional[str] = None) -> dict[str, Any]:
    """
    Ejecuta las 5 llamadas en el orden del contrato de software y devuelve
    el JSON consolidado tal como lo define el README:

        1. GET /repos/{owner}/{repo}
        2. GET /repos/{owner}/{repo}/languages
        3. GET /repos/{owner}/{repo}/contributors
        4. GET /repos/{owner}/{repo}/releases
        5. GET /repos/{owner}/{repo}/branches
    """
    repo_data = repo_endpoint.get_repo(owner, repo, token=token)
    languages_data = languages.get_languages(owner, repo, token=token)
    contributors_count = contributors.get_contributors_count(owner, repo, token=token)
    releases_count = releases.get_releases_count(owner, repo, token=token)
    branches_count = branches.get_branches_count(owner, repo, token=token)

    return {
        "id": repo_data["id"],
        "name": repo_data["name"],
        "owner": repo_data["owner"]["login"],
        "description": repo_data.get("description"),
        "stars": repo_data["stargazers_count"],
        "forks": repo_data["forks_count"],
        "watchers": repo_data["watchers_count"],
        "default_branch": repo_data["default_branch"],
        "languages": languages_data,
        "contributors_count": contributors_count,
        "branches_count": branches_count,
        "releases_count": releases_count,
    }


if __name__ == "__main__":
    import json
    import sys

    from http_client import GitHubAPIError

    if len(sys.argv) != 2 or "/" not in sys.argv[1]:
        print("Uso: python consolidate.py <owner>/<repo>")
        sys.exit(1)

    owner_arg, repo_arg = sys.argv[1].split("/", 1)

    try:
        result = consolidate(owner_arg, repo_arg)
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except GitHubAPIError as e:
        print(f"Error [{e.status_code}]: {e}")
        sys.exit(1)

