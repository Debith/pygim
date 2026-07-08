"""Public persistence module.

Exposes the C++ ``_persistence`` extension as ``DataStore`` with
``acquire_datastore`` factory.  Core C++ operates on Arrow exclusively.
Format conversion (Polars/Pandas) is a runtime attribute on the adapter,
not a template parameter.

``acquire_datastore`` accepts either a raw ODBC connection string or a
SQLAlchemy-style URL (``mssql+pyodbc://user:pass@host:port/db?driver=...``
or the ``?odbc_connect=<url-encoded DSN>`` form). The URL → ODBC DSN
translation now lives in the C++ core (``ConnectionString``), so both this
module's ``acquire_datastore`` and the raw extension entry point share it —
no SQLAlchemy dependency required.

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
    DataStoreSession = _ext.DataStoreSession
    Query = _ext.Query
    ConnectionString = _ext.ConnectionString
    GimError = _ext.GimError
    DataStoreIntegrityError = _ext.DataStoreIntegrityError
    DataStoreTransientError = _ext.DataStoreTransientError
    DataStoreDataError = _ext.DataStoreDataError

    def _translate_conn_str(conn_str):
        """Translate a SQLAlchemy-style URL into a raw ODBC connection string.

        Thin compatibility shim over ``ConnectionString`` (the translation now
        lives in C++ core). Raw DSNs (no ``://``) pass through untouched; URLs
        are parsed and rendered with credentials for connecting.
        """
        if "://" not in conn_str:
            return conn_str
        return ConnectionString.parse(conn_str).render(reveal=True)

    def acquire_datastore(conn_str, *args, **kwargs):
        """Create a DataStore from an ODBC DSN or SQLAlchemy-style URL.

        The connection string (raw DSN or ``mssql+pyodbc://`` URL) is passed
        straight to the extension, which parses it via ``ConnectionString`` —
        see the compiled ``pygim._persistence.acquire_datastore`` for the full
        parameter documentation.
        """
        return _ext.acquire_datastore(conn_str, *args, **kwargs)

    # Guard the concat: under ``python -OO`` docstrings are stripped to None.
    acquire_datastore.__doc__ = (
        (acquire_datastore.__doc__ or "")
        + "\n\n"
        + (_ext.acquire_datastore.__doc__ or "")
    )

    __all__ = [
        "Format",
        "DataStore",
        "DataStoreSession",
        "Query",
        "ConnectionString",
        "GimError",
        "DataStoreIntegrityError",
        "DataStoreTransientError",
        "DataStoreDataError",
        "acquire_datastore",
    ]
except ImportError:  # pragma: no cover – extension absent (Arrow/ODBC not installed)
    __all__ = []
