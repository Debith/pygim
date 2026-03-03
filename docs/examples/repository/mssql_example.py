"""Example usage of Repository with native MSSQL strategy.

Requires ODBC headers and Arrow C++ library installed at build time.
"""
from pygim import repository as rv2

CONNECTION = (
    "Driver={ODBC Driver 17 for SQL Server};"
    "Server=localhost;Database=test;UID=sa;PWD=Passw0rd!;"
)

repo = rv2.Repository(CONNECTION)


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
