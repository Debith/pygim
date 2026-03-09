"""
1.4 — Transpose correctness: RowMajorTranspose and ColumnMajorTranspose must
produce byte-identical round-trip results across all supported Arrow column types.

Skipped automatically when the STRESS_CONN environment variable is not set.
Requirements when running live:
  - SQL Server reachable via the connection string in STRESS_CONN
  - ODBC Driver 18 for SQL Server
  - pip install pyodbc polars
"""
from __future__ import annotations

import datetime
import os

import pytest

# ── Optional dependencies ────────────────────────────────────────────────────

try:
    import pyodbc as _pyodbc
except ModuleNotFoundError:
    _pyodbc = None  # type: ignore[assignment]

try:
    import polars as pl
except ModuleNotFoundError:
    pl = None  # type: ignore[assignment]

# ── Skip condition ───────────────────────────────────────────────────────────

STRESS_CONN = os.getenv("STRESS_CONN", "").strip()

requires_db = pytest.mark.skipif(
    not STRESS_CONN,
    reason="STRESS_CONN env var not set — skipping live DB correctness test",
)
requires_deps = pytest.mark.skipif(
    _pyodbc is None or pl is None,
    reason="pyodbc and polars required for correctness test",
)

# ── Test table schema ────────────────────────────────────────────────────────

_TABLE = "dbo._pygim_transpose_test"

_DDL_CREATE = f"""
IF OBJECT_ID(N'{_TABLE}', N'U') IS NULL
CREATE TABLE {_TABLE} (
    id       INT          NOT NULL PRIMARY KEY,
    val_i32  INT          NOT NULL,
    val_i64  BIGINT       NOT NULL,
    val_f64  FLOAT        NOT NULL,
    val_str  NVARCHAR(64) NOT NULL,
    val_date DATE         NOT NULL,
    val_ts   DATETIME2(6) NOT NULL
);
"""

_DDL_DROP = f"IF OBJECT_ID(N'{_TABLE}', N'U') IS NOT NULL DROP TABLE {_TABLE};"

_N_ROWS = 2_000  # small enough to be fast, large enough to span multiple mini-batches


# ── Fixtures ─────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def db_conn():
    """Return a live pyodbc connection; auto-cleans up the test table."""
    if not STRESS_CONN or _pyodbc is None:
        pytest.skip("STRESS_CONN not set or pyodbc missing")
    cn = _pyodbc.connect(STRESS_CONN, timeout=30)
    cur = cn.cursor()
    cur.execute(_DDL_DROP)
    cur.execute(_DDL_CREATE)
    cn.commit()
    cur.close()
    yield cn
    cur2 = cn.cursor()
    cur2.execute(_DDL_DROP)
    cn.commit()
    cur2.close()
    cn.close()


def _make_dataframe(n: int) -> "pl.DataFrame":
    """Build a small Polars DataFrame with all supported Arrow column types."""
    assert pl is not None
    epoch = datetime.date(1970, 1, 1)
    return pl.DataFrame({
        "id":       list(range(1, n + 1)),
        "val_i32":  [i % 100_000 for i in range(n)],
        "val_i64":  [i * 10_000_000_000 for i in range(n)],
        "val_f64":  [float(i) * 1.23456789 for i in range(n)],
        "val_str":  [f"row_{i:07d}" for i in range(n)],
        "val_date": [epoch + datetime.timedelta(days=i % 10_000) for i in range(n)],
        "val_ts":   [
            datetime.datetime(2020, 1, 1) + datetime.timedelta(microseconds=i * 1_000_003)
            for i in range(n)
        ],
    })


def _select_all(cn: "pyodbc.Connection") -> list[tuple]:
    """SELECT all rows from the test table, ordered by id."""
    cur = cn.cursor()
    cur.execute(f"SELECT id, val_i32, val_i64, val_f64, val_str, val_date, val_ts FROM {_TABLE} ORDER BY id")
    rows = cur.fetchall()
    cur.close()
    return rows


def _truncate(cn: "pyodbc.Connection") -> None:
    cur = cn.cursor()
    cur.execute(f"TRUNCATE TABLE {_TABLE}")
    cn.commit()
    cur.close()


# ── Test ─────────────────────────────────────────────────────────────────────

@requires_db
@requires_deps
def test_row_major_and_column_major_produce_identical_output(db_conn):
    """Round-trip through both strategies must yield byte-identical rows."""
    from pygim import acquire_repository

    df = _make_dataframe(_N_ROWS)

    # ── Insert via RowMajorTranspose ──────────────────────────────────────
    repo_row = acquire_repository(STRESS_CONN, transpose="row_major")
    repo_row.persist_dataframe(_TABLE, df, prefer_arrow=True)
    rows_row_major = _select_all(db_conn)
    assert len(rows_row_major) == _N_ROWS, "row_major: unexpected row count"

    _truncate(db_conn)

    # ── Insert via ColumnMajorTranspose ───────────────────────────────────
    repo_col = acquire_repository(STRESS_CONN, transpose="column_major")
    repo_col.persist_dataframe(_TABLE, df, prefer_arrow=True)
    rows_col_major = _select_all(db_conn)
    assert len(rows_col_major) == _N_ROWS, "column_major: unexpected row count"

    # ── Compare ───────────────────────────────────────────────────────────
    assert rows_row_major == rows_col_major, (
        f"Strategies disagree.\n"
        f"First differing row index: "
        f"{next(i for i, (a, b) in enumerate(zip(rows_row_major, rows_col_major)) if a != b)}"
    )
