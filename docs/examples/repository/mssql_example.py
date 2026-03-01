"""Example usage of Repository with native MSSQL strategy.

Requires building with ODBC headers (``PYGIM_HAVE_ODBC``) and enabling the native
MSSL strategy during installation.
"""
from pygim import repository_v2 as rv2

CONNECTION = (
    "Driver={ODBC Driver 17 for SQL Server};"
    "Server=localhost;Database=test;UID=sa;PWD=Passw0rd!;"
)

repo = rv2.Repository(transformers=False)
repo.add_memory_strategy()  # dev cache / fallback
repo.add_mssql_strategy(CONNECTION)


try:
    # Fluent param query (limit 1)
    query = (
        rv2.Query()
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
