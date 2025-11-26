"""Example usage of Repository with native MSSQL strategy (placeholder).

Requires building with ODBC headers (``PYGIM_HAVE_ODBC``) and enabling the native
``mssql_strategy`` extension during installation.
"""
from pygim import mssql_strategy, repository
from pygim.query import Query
from pygim.repo_helpers import MemoryStrategy

CONNECTION = (
    "Driver={ODBC Driver 17 for SQL Server};"
    "Server=localhost;Database=test;UID=sa;PWD=Passw0rd!;"
)

repo = repository.Repository(transformers=False)
repo.add_strategy(MemoryStrategy())  # dev cache / fallback
repo.add_strategy(mssql_strategy.MssqlStrategyNative(CONNECTION))


try:
    # Fluent param query (limit 1)
    query = (
        Query()
        .select(["id", "name", "email"])
        .from_table("users")
        .where("id=?", 42)
        .limit(1)
        .build()
    )
    rows = repo.fetch_raw(query)
    print("Query rows:", rows)
except Exception as e:  # pragma: no cover - environment dependent
    print("Save or fetch failed (likely missing native build or driver):", e)
