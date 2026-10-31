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

A query is built and rendered from plain values — no store, no connection::

    from pygim.persistence import MssqlDialect, Query

    q = Query().select("id").from_table("dbo.users").where("id > 5").limit(10)
    MssqlDialect().render(q)   # 'SELECT TOP 10 [id] FROM [dbo].[users] WHERE id > 5'
    store.load(q)              # runs that SQL on the store's connection
"""

# Load Arrow's shared libraries into the process *before* the extension, so the
# dynamic loader resolves libarrow/libparquet from the pyarrow package itself
# rather than from whatever RPATH happened to be baked in at build time.
import pyarrow
import pyarrow.parquet  # noqa: F401

# The extension is mandatory: if it is missing, the ImportError must surface.
from pygim import _persistence as _ext  # type: ignore

Format = _ext.Format
DataStore = _ext.DataStore
acquire_datastore = _ext.acquire_datastore
Query = _ext.Query
MssqlDialect = _ext.MssqlDialect
PersistenceError = _ext.PersistenceError

__all__ = [
    "Format",
    "DataStore",
    "acquire_datastore",
    "Query",
    "MssqlDialect",
    "PersistenceError",
]
