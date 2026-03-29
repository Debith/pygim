"""Public repository module.

Exposes the C++ ``_repository`` extension with ``acquire_repo`` factory.
Core C++ operates on Arrow exclusively. Format conversion (Polars/Pandas)
is a runtime attribute on the adapter, not a template parameter.

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

Format = _ext.Format
Repository = _ext.Repository
acquire_repo = _ext.acquire_repo

__all__ = [
    "Format",
    "Repository",
    "acquire_repo",
]
