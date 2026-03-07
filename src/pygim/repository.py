"""Public repository module.

Exposes the C++ ``_repository`` extension symbols plus the
``acquire_repository`` factory function.

All connection-string normalisation (ODBC pass-through, URL parsing,
validation) is handled in C++ by ``_repository`` / ``connection_uri.h``.

Accepted formats::

    memory://                                        -- in-memory (dev/test)
    mssql://server/db                                -- URL form
    Driver={ODBC Driver 18 for SQL Server};Server=… -- raw ODBC (passed through as-is)

Usage::

    from pygim import repository

    repo = repository.acquire_repository(
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost,1433;"
        "Database=mydb;TrustServerCertificate=yes;",
        transformers=False,
    )
"""

import re

from pygim import _repository as _repo_ext

Repository = _repo_ext.Repository
Query = _repo_ext.Query


def _display_uri(conn_str: str) -> str:
    """Return a printable, credential-free summary of *conn_str*.

    - URL form  ``mssql://user:secret@host/db`` → ``mssql://user:***@host/db``
    - Raw ODBC  ``…;PWD=secret;…``              → ``…;PWD=***;…``
    - ``memory://``                              → returned unchanged
    """
    # URL password: scheme://user:PASSWORD@host
    result = re.sub(r"(://[^:@/]*:)[^@]*(@)", r"\1***\2", conn_str)
    # Raw ODBC PWD= (case-insensitive)
    result = re.sub(r"(?i)(PWD=)[^;]*", r"\1***", result)
    return result


def acquire_repository(
    conn_str: str,
    *,
    transformers: bool = False,
    quiet: bool = False,
) -> Repository:
    """Create a :class:`Repository` from any supported connection string.

    Parameters
    ----------
    conn_str:
        Any of the accepted formats:

        * ``memory://`` — in-memory strategy (dev/test).
        * ``mssql://server/db`` — URL form.
        * ``Driver={...};Server=…`` — raw ODBC string, passed through unchanged.
    transformers:
        Enable the pre-save / post-load transformer pipeline.
    quiet:
        Suppress the one-line status message printed to stdout before the
        connection is established.  Defaults to ``False`` (message shown).
        Has no effect on the logging subsystem.

    Returns
    -------
    Repository
        A fully initialised repository backed by the appropriate strategy.
    """
    if not quiet:
        print(f"connecting  {_display_uri(conn_str)}", flush=True)
    return Repository(conn_str, transformers)


__all__ = [
    "Repository",
    "Query",
    "acquire_repository",
]
