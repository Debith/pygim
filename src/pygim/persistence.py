"""Public persistence module.

Exposes the C++ ``_persistence`` extension as ``DataStore`` with
``acquire_datastore`` factory.  Core C++ operates on Arrow exclusively.
Format conversion (Polars/Pandas) is a runtime attribute on the adapter,
not a template parameter.

Usage::

    from pygim.persistence import acquire_datastore

    store = acquire_datastore(
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost,1433;"
        "Database=mydb;TrustServerCertificate=yes;",
        format="polars",
        batch_size=100_000,
        bcp_workers=4,
    )

    import polars as pl
    df = pl.DataFrame({"id": [1, 2, 3], "name": ["a", "b", "c"]})
    metrics = store.save(df, "dbo.my_table")
    print(f"Saved {metrics['processed_rows']} rows in {metrics['total_seconds']:.2f}s")
"""

try:
    from pygim import _persistence as _ext  # type: ignore

    Format = _ext.Format
    DataStore = _ext.DataStore
    acquire_datastore = _ext.acquire_datastore

    __all__ = [
        "Format",
        "DataStore",
        "acquire_datastore",
    ]
except ImportError:  # pragma: no cover – extension absent (Arrow/ODBC not installed)
    __all__ = []
