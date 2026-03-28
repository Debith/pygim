"""Public repository module.

Exposes the C++ ``_repository`` extension (target architecture placeholder)
plus the ``acquire_repo`` factory function.

Target architecture (Polars → Arrow → MSSQL):
    - Core C++ knows only Arrow — no Python, no Polars/Pandas.
    - Adapter (pybind11) handles format conversion at the edge.
    - FlexibleRepository wraps FormatAdapter with optional transforms.

Usage::

    from pygim.repository import acquire_repo

    repo = acquire_repo(
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost,1433;"
        "Database=mydb;TrustServerCertificate=yes;",
        format="polars",
    )
    repo.save("dbo.my_table")
    repo.load("dbo.my_table")
"""

from pygim import _repository as _ext

Query = _ext.Query
acquire_repo = _ext.acquire_repo


__all__ = [
    "Query",
    "acquire_repo",
]
