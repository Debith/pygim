"""Example demonstrating saving (upsert) with Repository + native MSSQL strategy.

NOTE: The native strategy is a skeleton. Real production usage MUST parameterize queries and
avoid string concatenation (risk of SQL injection). Build with:
    CFLAGS='-DPYGIM_ENABLE_MSSQL' pip install -e .
if ODBC headers/libs are present (unixODBC + msodbcsql17 driver).
"""
#from pygim import repository as repository_ext # with builder, we should not need to know this
#from pygim.repo_helpers import MemoryStrategy  # with builder, we should not need to know this
#from pygim import mssql_strategy as mssql_ext  # nor this
from pygim.repo import create_repository


repo = create_repository("Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=test;UID=sa;PWD=Passw0rd!;")


try:
    # Fluent query (director exposed): parameterized fetch limited to 1 row
    rows = (
        repo.query
            .select(["id", "name", "email"])  # columns
            .from_table("users")                 # table
            .where("id=?", 42)                   # simple predicate
            .limit(1)                             # row limit
            .fetch()                              # execute returning list[dict] (MSSQL) or None
    )

    print("Query rows:", rows)
except Exception as e:  # pragma: no cover - example environment dependent
    print("Save or fetch failed (likely missing native build or driver):", e)
