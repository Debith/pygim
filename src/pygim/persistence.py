"""Public persistence module.

Exposes the C++ ``_persistence`` extension as ``DataStore`` with
``acquire_datastore`` factory.  Core C++ operates on Arrow exclusively.
Format conversion (Polars/Pandas) is a runtime attribute on the adapter,
not a template parameter.

``acquire_datastore`` accepts either a raw ODBC connection string or a
SQLAlchemy-style URL (``mssql+pyodbc://user:pass@host:port/db?driver=...``
or the ``?odbc_connect=<url-encoded DSN>`` form), which is translated to an
ODBC DSN internally — no SQLAlchemy dependency required.

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

from urllib.parse import parse_qs, unquote, urlparse


def _dsn_quote(value):
    """Brace-quote an ODBC attribute value when it needs it.

    ODBC connection strings are ``;``-delimited ``key=value`` pairs. A value
    containing ``;``, ``{``, ``}`` or surrounding whitespace must be wrapped in
    braces (with embedded ``}`` doubled), or it would break the string or inject
    extra attributes — e.g. a password containing ``;``. Plain values are left
    bare so common cases stay readable.
    """
    if value != value.strip() or any(ch in value for ch in ";{}"):
        return "{" + value.replace("}", "}}") + "}"
    return value


def _translate_conn_str(conn_str):
    """Translate a SQLAlchemy-style URL into a raw ODBC connection string.

    Raw DSNs (no ``://``) pass through untouched. The ``odbc_connect`` query
    parameter, when present, wins verbatim (it already is a full DSN). The
    hostless form ``mssql+pyodbc://user:pass@dsnname`` (no port, database, or
    driver) is treated as a named ODBC ``DSN=`` reference.
    """
    if "://" not in conn_str:
        return conn_str

    parsed = urlparse(conn_str)
    if not parsed.scheme.startswith("mssql"):
        raise ValueError(
            f"Unsupported URL scheme '{parsed.scheme}': expected an "
            "mssql+pyodbc:// URL or a raw ODBC connection string"
        )

    query = parse_qs(parsed.query, keep_blank_values=True)
    if "odbc_connect" in query:
        return query["odbc_connect"][0]

    driver = query.pop("driver", [None])[0]
    # urlparse lowercases and percent-encodes the host; unquote restores
    # instance names written as ``host%5CSQLEXPRESS``.
    host = unquote(parsed.hostname) if parsed.hostname else ""
    database = parsed.path.lstrip("/")

    def _creds_and_query(parts):
        if parsed.username:
            parts.append(f"UID={_dsn_quote(unquote(parsed.username))}")
        if parsed.password:
            parts.append(f"PWD={_dsn_quote(unquote(parsed.password))}")
        for key, values in query.items():
            parts.append(f"{key}={_dsn_quote(values[0])}")
        return ";".join(parts) + ";"

    # Named-DSN form: a bare host with no port/database/driver is a DSN name.
    if host and not driver and not parsed.port and not database:
        return _creds_and_query([f"DSN={_dsn_quote(host)}"])

    parts = []
    if driver:
        if not driver.startswith("{"):
            driver = "{" + driver + "}"
        parts.append(f"Driver={driver}")
    server = f"Server={host or 'localhost'}"
    if parsed.port:
        server += f",{parsed.port}"
    parts.append(server)
    if database:
        parts.append(f"Database={database}")
    return _creds_and_query(parts)


try:
    from pygim import _persistence as _ext  # type: ignore

    Format = _ext.Format
    DataStore = _ext.DataStore
    DataStoreSession = _ext.DataStoreSession
    Query = _ext.Query
    GimError = _ext.GimError
    DataStoreIntegrityError = _ext.DataStoreIntegrityError
    DataStoreTransientError = _ext.DataStoreTransientError
    DataStoreDataError = _ext.DataStoreDataError

    def acquire_datastore(conn_str, *args, **kwargs):
        """Create a DataStore from an ODBC DSN or SQLAlchemy-style URL.

        See the compiled ``pygim._persistence.acquire_datastore`` for the
        full parameter documentation; this wrapper only adds URL
        translation for ``mssql+pyodbc://`` connection strings.
        """
        return _ext.acquire_datastore(_translate_conn_str(conn_str), *args, **kwargs)

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
        "GimError",
        "DataStoreIntegrityError",
        "DataStoreTransientError",
        "DataStoreDataError",
        "acquire_datastore",
    ]
except ImportError:  # pragma: no cover – extension absent (Arrow/ODBC not installed)
    __all__ = []
