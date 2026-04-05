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
        batch_size=100_000,
        bcp_workers=4,
    )

    import polars as pl
    df = pl.DataFrame({"id": [1, 2, 3], "name": ["a", "b", "c"]})
    metrics = repo.save(df, "dbo.my_table")
    print(f"Saved {metrics['processed_rows']} rows in {metrics['total_seconds']:.2f}s")
"""

try:
    from pygim import _repository as _ext  # type: ignore

    Format = _ext.Format
    Repository = _ext.Repository
    acquire_repo = _ext.acquire_repo

    __all__ = [
        "Format",
        "Repository",
        "acquire_repo",
    ]
except ImportError:  # pragma: no cover – extension absent (Arrow/ODBC not installed)
    __all__ = []
