"""Example usage of Repository with native MSSQL strategy (placeholder).

Requires building with -DPYGIM_ENABLE_MSSQL and implementing native ODBC calls.
"""
#from pygim import repository as repository_ext # with builder, we should not need to know this
#from pygim.repo_helpers import MemoryStrategy  # with builder, we should not need to know this
#from pygim import mssql_strategy as mssql_ext  # nor this
from pygim.repo import create_repository


repo = create_repository("Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=test;UID=sa;PWD=Passw0rd!;")


try:
    # Fluent param query (limit 1)
    rows = (
        repo.query
            .select(["id","name","email"])
            .from_table("users")
            .where("id=?", 42)
            .limit(1)
            .fetch()
    )
    print("Query rows:", rows)
except Exception as e:  # pragma: no cover - environment dependent
    print("Save or fetch failed (likely missing native build or driver):", e)
