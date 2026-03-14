#!/usr/bin/env python3
"""
BCP throughput benchmark with multiple dataset profiles.

Runs identical BCP insert logic against three table schemas so you can
compare apples-to-apples across simple, mixed, and complex column sets.
Optionally loads (reads) data back to benchmark round-trip throughput.

Profiles:
  simple     – 7 columns  (INT, BIGINT, FLOAT, NVARCHAR, DATE, DATETIME2)
  mixed      – 9 columns  (adds DECIMAL, wider NVARCHAR)
  complex    – 11 columns (adds BIT, DECIMAL, NCHAR, UNIQUEIDENTIFIER)
  exhaustive – 19 columns (every supported Arrow type: INT8-64, UINT8-64,
                            BOOL, FLOAT32/64, DATE, TIME, DATETIME2,
                            DURATION, NVARCHAR, VARBINARY)

Modes:
  both  – write, read-back, and verify round-trip integrity (default)
  write – BCP insert only
  load  – SELECT read only (table must already contain data)

Usage:
  # Single profile — writes, reads back, verifies (default)
  python benchmarks/bcp_throughput.py --rows 500000 --dataset simple

  # Write-only (skip read-back and verification)
  python benchmarks/bcp_throughput.py --rows 500000 --dataset simple --mode write

  # Compare row_major vs column_major for one profile
  python benchmarks/bcp_throughput.py --rows 500000 --dataset mixed --compare-strategies

  # Run ALL profiles side-by-side
  python benchmarks/bcp_throughput.py --rows 500000 --dataset all

  # Full matrix: every profile × both strategies
  python benchmarks/bcp_throughput.py --rows 500000 --dataset all --compare-strategies

  # Load (read) benchmark after data is already in the table
  python benchmarks/bcp_throughput.py --rows 500000 --dataset simple --mode load

  # Disable verification (keep load benchmark but skip cell comparison)
  python benchmarks/bcp_throughput.py --rows 500000 --mode both --no-verify

Connection:
  Set STRESS_CONN env var, or pass --conn, or let it fall back to the
  local dev default.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import platform
import sys
import time
from datetime import date as py_date
from datetime import datetime, timezone
from decimal import Decimal

print(f"PID: {os.getpid()}")  # attach profiler here if needed


# ── Dependency helpers ───────────────────────────────────────────────────────

def _require(mod: str):
    try:
        import importlib
        return importlib.import_module(mod)
    except ModuleNotFoundError:
        print(f"ERROR: '{mod}' is required. Install with: pip install {mod}", file=sys.stderr)
        sys.exit(1)


def default_connection_string() -> str:
    pwd = os.getenv("MSSQL_SA_PASSWORD", "NewP@ssw0rd#2025")
    return (
        f"Driver={{ODBC Driver 18 for SQL Server}};Server=localhost,1433;Database=master;"
        f"UID=sa;PWD={pwd};Encrypt=yes;TrustServerCertificate=yes;"
    )


# ── Dataset profiles ─────────────────────────────────────────────────────────
#
# Each profile is a dict with:
#   table    – fully qualified table name
#   ddl      – IF NOT EXISTS create DDL
#   generate – callable(n, pl) -> polars.DataFrame
#   columns  – int (for display)

import pygim


# ── Schema definitions for pygim.create_df ───────────────────────────────────
# Each maps column names -> type strings consumed by the C++ data generator.

_SCHEMA_SIMPLE = {
    "id":       "serial",
    "val_i32":  "int32",
    "val_i64":  "int64",
    "val_f64":  "float64",
    "val_str":  "string",
    "val_date": "date",
    "val_ts":   "timestamp",
}


_SCHEMA_MIXED = {
    "id":        "serial",
    "val_i32":   "int32",
    "val_i64":   "int64",
    "val_f64":   "float64",
    "val_dec":   "float64",   # maps to DECIMAL(18,4) on SQL side
    "val_str":   "string",
    "val_text":  "string",
    "val_date":  "date",
    "val_ts":    "timestamp",
}


_SCHEMA_COMPLEX = {
    "id":             "serial",
    "col_int":        "int32",
    "col_bigint":     "int64",
    "col_bit":        "bool",
    "col_decimal":    "float64",
    "col_float":      "float64",
    "col_nvarchar":   "string",
    "col_nchar":      "string",
    "col_date":       "date",
    "col_datetime2":  "timestamp",
    "col_uuid":       "uuid",
}


_SCHEMA_EXHAUSTIVE = {
    "id":              "serial",
    "col_int8":        "int8",
    "col_int16":       "int16",
    "col_int32":       "int32",
    "col_int64":       "int64",
    "col_uint8":       "uint8",
    "col_uint16":      "uint16",
    "col_uint32":      "uint32",
    "col_uint64":      "uint64",
    "col_bool":        "bool",
    "col_float32":     "float32",
    "col_float64":     "float64",
    "col_date":        "date",
    "col_time":        "time",
    "col_datetime2":   "timestamp",
    "col_duration_us": "duration",
    "col_nvarchar":    "string",
    "col_binary":      "binary",
}


def _generate_simple(n: int, pl) -> object:
    """7 columns: id, INT, BIGINT, FLOAT, NVARCHAR(64), DATE, DATETIME2(6)."""
    return pygim.create_df(_SCHEMA_SIMPLE, rows=n)


def _generate_mixed(n: int, pl) -> object:
    """9 columns: simple + DECIMAL(18,4), wider NVARCHAR(200)."""
    return pygim.create_df(_SCHEMA_MIXED, rows=n)


def _generate_complex(n: int, pl) -> object:
    """11 columns: INT PK + INT, BIGINT, BIT, DECIMAL, FLOAT, NVARCHAR, NCHAR, DATE, DATETIME2, UUID."""
    return pygim.create_df(_SCHEMA_COMPLEX, rows=n)


def _generate_exhaustive(n: int, pl) -> object:
    """18 columns: exercises every Arrow type dispatch path in the BCP binder."""
    return pygim.create_df(_SCHEMA_EXHAUSTIVE, rows=n)


PROFILES: dict[str, dict] = {
    "simple": {
        "table": "dbo.bcp_bench_simple",
        "ddl": """
DROP TABLE IF EXISTS dbo.bcp_bench_simple;
CREATE TABLE dbo.bcp_bench_simple (
    id       INT           NOT NULL PRIMARY KEY,
    val_i32  INT           NOT NULL,
    val_i64  BIGINT        NOT NULL,
    val_f64  FLOAT         NOT NULL,
    val_str  NVARCHAR(100) NOT NULL,
    val_date DATE          NOT NULL,
    val_ts   DATETIME2(6)  NOT NULL
);""",
        "generate": _generate_simple,
        "columns": 7,
    },
    "mixed": {
        "table": "dbo.bcp_bench_mixed",
        "ddl": """
DROP TABLE IF EXISTS dbo.bcp_bench_mixed;
CREATE TABLE dbo.bcp_bench_mixed (
    id        INT            NOT NULL PRIMARY KEY,
    val_i32   INT            NOT NULL,
    val_i64   BIGINT         NOT NULL,
    val_f64   FLOAT          NOT NULL,
    val_dec   DECIMAL(18,4)  NOT NULL,
    val_str   NVARCHAR(100)  NOT NULL,
    val_text  NVARCHAR(200)  NOT NULL,
    val_date  DATE           NOT NULL,
    val_ts    DATETIME2(6)   NOT NULL
);""",
        "generate": _generate_mixed,
        "columns": 9,
    },
    "complex": {
        "table": "dbo.bcp_bench_complex",
        "ddl": """
DROP TABLE IF EXISTS dbo.bcp_bench_complex;
CREATE TABLE dbo.bcp_bench_complex (
    id             INT                NOT NULL PRIMARY KEY,
    col_int        INT                NOT NULL,
    col_bigint     BIGINT             NOT NULL,
    col_bit        BIT                NOT NULL,
    col_decimal    DECIMAL(18,4)      NOT NULL,
    col_float      FLOAT              NOT NULL,
    col_nvarchar   NVARCHAR(100)      NOT NULL,
    col_nchar      NVARCHAR(30)       NOT NULL,
    col_date       DATE               NOT NULL,
    col_datetime2  DATETIME2(6)       NOT NULL,
    col_uuid       UNIQUEIDENTIFIER   NOT NULL
);""",
        "generate": _generate_complex,
        "columns": 11,
    },
    "exhaustive": {
        "table": "dbo.bcp_bench_exhaustive",
        "ddl": """
DROP TABLE IF EXISTS dbo.bcp_bench_exhaustive;
CREATE TABLE dbo.bcp_bench_exhaustive (
    id              INT             NOT NULL PRIMARY KEY,
    col_int8        TINYINT         NOT NULL,
    col_int16       SMALLINT        NOT NULL,
    col_int32       INT             NOT NULL,
    col_int64       BIGINT          NOT NULL,
    col_uint8       TINYINT         NOT NULL,
    col_uint16      SMALLINT        NOT NULL,
    col_uint32      INT             NOT NULL,
    col_uint64      BIGINT          NOT NULL,
    col_bool        BIT             NOT NULL,
    col_float32     REAL            NOT NULL,
    col_float64     FLOAT           NOT NULL,
    col_date        DATE            NOT NULL,
    col_time        TIME(7)         NOT NULL,
    col_datetime2   DATETIME2(6)    NOT NULL,
    col_duration_us BIGINT          NOT NULL,
    col_nvarchar    NVARCHAR(100)   NOT NULL,
    col_binary      VARBINARY(256)  NOT NULL
);""",
        "generate": _generate_exhaustive,
        "columns": 18,
    },
}

PROFILE_ORDER = ["simple", "mixed", "complex", "exhaustive"]


# ── SQL helpers ──────────────────────────────────────────────────────────────

def ensure_table(conn_str: str, ddl: str, pyodbc) -> None:
    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = True
    cn.execute(ddl)
    cn.close()


def truncate_table(conn_str: str, table: str, pyodbc) -> None:
    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = True
    cn.execute(f"TRUNCATE TABLE {table}")
    cn.close()


# ── Payload size estimation ──────────────────────────────────────────────────

def estimate_dataframe_size_bytes(frame) -> int:
    """Best-effort estimation of Polars DataFrame size in bytes."""
    for attr in ("estimated_size", "estimated_size_bytes"):
        estimator = getattr(frame, attr, None)
        if callable(estimator):
            try:
                size = estimator()
            except Exception:
                continue
            if size is not None:
                return max(int(size), 0)
    try:
        return max(int(frame.__sizeof__()), 0)
    except Exception:
        return 0


# ── Benchmark runner ─────────────────────────────────────────────────────────

def run_one(
    conn_str: str,
    table: str,
    df,
    strategy: str,
    bcp_batch_size: int,
    table_hint: str,
    bcp_workers: int = 0,
) -> dict:
    """Insert *df* via BCP and return timing metrics."""
    from pygim import acquire_repository

    repo = acquire_repository(conn_str, transpose=strategy)

    payload_bytes = estimate_dataframe_size_bytes(df)
    nrows = len(df)

    t0 = time.perf_counter()
    result = repo.persist_dataframe(
        table,
        df,
        prefer_arrow=True,
        bcp_batch_size=bcp_batch_size,
        table_hint=table_hint,
        bcp_workers=bcp_workers,
    )
    elapsed = time.perf_counter() - t0

    mb_s = (payload_bytes / 1_048_576) / elapsed if elapsed > 0 else 0.0

    return {
        "strategy":      strategy,
        "rows":          nrows,
        "elapsed_s":     elapsed,
        "payload_bytes": payload_bytes,
        "mb_s":          mb_s,
        "rows_s":        nrows / elapsed if elapsed > 0 else 0.0,
        "bcp_metrics":   result,
    }

# ── Load (read) benchmark runner ─────────────────────────────────────────

def run_load(conn_str: str, table: str, profile_name: str) -> dict:
    """Load all rows from *table* via C++ Repository.load() (Arrow → Polars)."""
    from pygim import acquire_repository

    repo = acquire_repository(conn_str)
    t0 = time.perf_counter()
    df = repo.load(table)
    elapsed = time.perf_counter() - t0

    nrows = len(df)
    payload_bytes = df.estimated_size() if nrows > 0 else 0
    mb_s = (payload_bytes / 1_048_576) / elapsed if elapsed > 0 else 0.0

    return {
        "direction":     "load",
        "profile":       profile_name,
        "rows":          nrows,
        "elapsed_s":     elapsed,
        "payload_bytes": payload_bytes,
        "mb_s":          mb_s,
        "rows_s":        nrows / elapsed if elapsed > 0 else 0.0,
    }

# ── Round-trip verification ───────────────────────────────────────────────────

def _polars_row_to_pydict(df, idx: int) -> dict:
    """Extract row *idx* from a Polars DataFrame as {col: python_value}."""
    return {col: df[col][idx] for col in df.columns}


def _values_match(source, actual, col_name: str, *, float_tol: float = 1e-6) -> bool:
    """Compare a single cell value from the source DataFrame with the SQL read-back.

    Handles type coercions introduced by Arrow → BCP → SQL → pyodbc:
      - Polars int → Python int (exact; signed int8 ↔ TINYINT unsigned handled)
      - Polars float → Python float / Decimal (relative tolerance)
      - Polars str → Python str (case-insensitive for UUIDs, rstrip for NCHAR)
      - Polars date → datetime.date (exact)
      - Polars datetime → datetime.datetime (1ms tolerance for DATETIME2(6))
      - Polars bool → Python bool / int 0|1 (BIT)
      - Polars timedelta → BIGINT microseconds
      - bytes / binary → bytes (exact)
      - None / null → None (exact)
    """
    import datetime as _dt

    if source is None and actual is None:
        return True
    if source is None or actual is None:
        return False

    # Bool (BIT comes back as bool or int)
    if isinstance(source, bool):
        return bool(source) == bool(actual)

    # timedelta (duration) → BIGINT microseconds
    if isinstance(source, _dt.timedelta):
        # Use integer arithmetic to avoid float precision loss
        src_us = (source.days * 86_400_000_000
                  + source.seconds * 1_000_000
                  + source.microseconds)
        if isinstance(actual, int):
            return src_us == actual
        if isinstance(actual, _dt.timedelta):
            return source == actual
        return str(source) == str(actual)

    # Integer types — handle signed↔unsigned wrapping (int8 → TINYINT)
    if isinstance(source, int) and isinstance(actual, int):
        if source == actual:
            return True
        # Signed int8 stored in unsigned TINYINT: source & 0xFF == actual
        if -128 <= source < 0 and 0 <= actual <= 255:
            return (source & 0xFF) == actual
        # Signed int16 stored in unsigned SMALLINT (if applicable)
        if -32768 <= source < 0 and 0 <= actual <= 65535:
            return (source & 0xFFFF) == actual
        return False

    # int vs float
    if isinstance(source, int) and isinstance(actual, float):
        return float(source) == actual
    if isinstance(source, float) and isinstance(actual, int):
        return source == float(actual)

    # Float types — relative tolerance; extra slack for SQL DECIMAL truncation
    if isinstance(source, float) and isinstance(actual, (float, int, Decimal)):
        a, b = float(source), float(actual)
        if math.isnan(a) and math.isnan(b):
            return True
        if a == 0 and b == 0:
            return True
        # Use generous tolerance for DECIMAL(18,4) columns — up to 5e-5 relative
        tol = max(float_tol, 5e-5) if isinstance(actual, Decimal) else float_tol
        return abs(a - b) <= tol * max(abs(a), abs(b), 1.0)

    # Decimal from SQL without float source
    if isinstance(actual, Decimal):
        try:
            tol = max(float_tol, 5e-5)
            return abs(float(source) - float(actual)) <= tol * max(abs(float(source)), 1.0)
        except (TypeError, ValueError):
            return str(source) == str(actual)

    # String — case-insensitive for UUIDs, rstrip for NCHAR padding
    if isinstance(source, str):
        s, a = source.strip(), str(actual).rstrip()
        if s == a:
            return True
        # UUID: SQL Server returns uppercase
        if s.lower() == a.lower():
            return True
        return False

    # Date
    if isinstance(source, py_date) and not isinstance(source, datetime):
        if isinstance(actual, py_date):
            return source == (actual.date() if isinstance(actual, datetime) else actual)

    # Datetime (microsecond tolerance)
    if isinstance(source, datetime) and isinstance(actual, datetime):
        diff = abs((source - actual).total_seconds())
        return diff < 0.001  # 1ms tolerance for DATETIME2(6)

    # bytes / binary
    if isinstance(source, (bytes, bytearray)) and isinstance(actual, (bytes, bytearray)):
        return bytes(source) == bytes(actual)

    # Generic fallback — compare string representations
    try:
        return str(source) == str(actual)
    except Exception:
        return False


def verify_round_trip(
    conn_str: str,
    table: str,
    source_df,
    profile_name: str,
    pyodbc,
    *,
    sample_size: int = 200,
) -> dict:
    """Read back data and verify it matches *source_df*.

    Checks:
      1. Row count matches.
      2. Column names match.
      3. A random sample of rows matches cell-by-cell.

    Returns a dict with verification results.
    """
    import random

    cn = pyodbc.connect(conn_str, timeout=60)
    cursor = cn.cursor()

    # 1. Row count
    cursor.execute(f"SELECT COUNT(*) FROM {table}")
    db_count = cursor.fetchone()[0]
    src_count = len(source_df)
    count_ok = db_count == src_count

    # 2. Column names
    cursor.execute(f"SELECT TOP 0 * FROM {table}")
    db_cols = [d[0] for d in cursor.description]
    src_cols = list(source_df.columns)
    cols_ok = db_cols == src_cols

    # 3. Sample row comparison
    sample_ids = sorted(random.sample(range(1, src_count + 1), min(sample_size, src_count)))
    # Fetch sample rows by id (assumes PK column is 'id' and 1-based serial)
    id_list = ",".join(str(i) for i in sample_ids)
    cursor.execute(f"SELECT * FROM {table} WHERE id IN ({id_list}) ORDER BY id")
    db_rows = cursor.fetchall()
    cursor.close()
    cn.close()

    mismatches = []
    rows_checked = 0
    for db_row in db_rows:
        row_id = db_row[0]  # first column is 'id'
        # source_df 'id' is 1-based serial, Polars index is 0-based
        src_idx = row_id - 1
        if src_idx < 0 or src_idx >= src_count:
            mismatches.append({"id": row_id, "error": "id out of source range"})
            continue
        rows_checked += 1
        src_row = _polars_row_to_pydict(source_df, src_idx)
        for ci, col in enumerate(db_cols):
            src_val = src_row.get(col)
            db_val = db_row[ci]
            if not _values_match(src_val, db_val, col):
                mismatches.append({
                    "id": row_id, "column": col,
                    "source": repr(src_val), "source_type": type(src_val).__name__,
                    "actual": repr(db_val), "actual_type": type(db_val).__name__,
                })

    passed = count_ok and cols_ok and len(mismatches) == 0
    return {
        "passed":        passed,
        "row_count_ok":  count_ok,
        "src_rows":      src_count,
        "db_rows":       db_count,
        "cols_ok":       cols_ok,
        "src_cols":      src_cols,
        "db_cols":       db_cols,
        "rows_checked":  rows_checked,
        "mismatches":    mismatches[:20],  # cap detail output
        "total_mismatches": len(mismatches),
    }


def print_verify_result(v: dict, profile: str) -> None:
    """Pretty-print verification outcome."""
    status = "PASS" if v["passed"] else "FAIL"
    parts = [
        f"  [{profile:>7s}]  verify={status}",
        f"rows={v['src_rows']:,}/{v['db_rows']:,}",
        f"cols={'OK' if v['cols_ok'] else 'MISMATCH'}",
        f"sample={v['rows_checked']:,} checked",
    ]
    if v["total_mismatches"] > 0:
        parts.append(f"mismatches={v['total_mismatches']}")
    print("  ".join(parts))

    if not v["passed"]:
        if not v["row_count_ok"]:
            print(f"           Row count: source={v['src_rows']:,}  db={v['db_rows']:,}")
        if not v["cols_ok"]:
            print(f"           Columns: source={v['src_cols']}")
            print(f"                    db    ={v['db_cols']}")
        for m in v["mismatches"][:10]:
            if "column" in m:
                print(
                    f"           id={m['id']}  col={m['column']}  "
                    f"source={m['source']} ({m['source_type']})  "
                    f"actual={m['actual']} ({m['actual_type']})"
                )
            else:
                print(f"           id={m['id']}  {m.get('error', '?')}")
        if v["total_mismatches"] > 10:
            print(f"           … and {v['total_mismatches'] - 10} more")


# ── Reporting ────────────────────────────────────────────────────────────────

def print_result(r: dict, profile: str) -> None:
    payload_mb = r["payload_bytes"] / 1_048_576
    print(
        f"  [{profile:>7s}]  strategy={r['strategy']!r:16s}  "
        f"rows={r['rows']:>10,}  "
        f"elapsed={r['elapsed_s']:6.2f}s  "
        f"rows/s={r['rows_s']:>12,.0f}  "
        f"MB/s={r['mb_s']:7.2f}  "
        f"payload={payload_mb:6.1f} MB"
    )
    bcp = r.get("bcp_metrics")
    if isinstance(bcp, dict):
        mode = bcp.get("mode", "?")
        bcp_m = bcp.get("bcp_metrics")
        if isinstance(bcp_m, dict):
            simd_level = str(bcp_m.get("simd_level", "scalar"))
            timing_level = str(bcp_m.get("timing_level", "stage"))
            row_loop = float(bcp_m.get("row_loop_seconds", 0))
            fixed_copy = float(bcp_m.get("fixed_copy_seconds", 0))
            redirect = float(bcp_m.get("colptr_redirect_seconds", 0))
            string_pack = float(bcp_m.get("string_pack_seconds", 0))
            sendrow = float(bcp_m.get("sendrow_seconds", 0))
            batch_flush = float(bcp_m.get("batch_flush_seconds", 0))
            total = float(bcp_m.get("total_seconds", 0))
            sent = int(bcp_m.get("sent_rows", 0))
            print(
                f"           mode={mode}  simd={simd_level}  timing={timing_level}  "
                f"row_loop={row_loop:.3f}s  copy={fixed_copy:.3f}s  "
                f"redirect={redirect:.3f}s  string={string_pack:.3f}s  "
                f"sendrow={sendrow:.3f}s  batch_flush={batch_flush:.3f}s  "
                f"bcp_total={total:.3f}s  sent_rows={sent:,}"
            )
        else:
            print(f"           mode={mode}  bcp_metrics={bcp}")


def print_load_result(r: dict) -> None:
    """Pretty-print a single load (read) result."""
    payload_mb = r["payload_bytes"] / 1_048_576
    print(
        f"  [{r['profile']:>7s}]  "
        f"rows={r['rows']:>10,}  "
        f"elapsed={r['elapsed_s']:6.2f}s  "
        f"rows/s={r['rows_s']:>12,.0f}  "
        f"MB/s={r['mb_s']:7.2f}  "
        f"payload={payload_mb:6.1f} MB"
    )


def print_comparison_table(results: list[dict]) -> None:
    """Print a compact comparison table across all runs (write + load)."""
    from tabulate import tabulate

    rows = []
    for r in results:
        payload_mb = r["payload_bytes"] / 1_048_576
        direction = r.get("direction", "write")
        strategy_or_method = r.get("strategy", "cpp" if direction == "load" else "-")
        simd_level = "-"
        timing_level = "-"
        bcp = r.get("bcp_metrics")
        if isinstance(bcp, dict):
            bcp_m = bcp.get("bcp_metrics")
            if isinstance(bcp_m, dict):
                simd_level = str(bcp_m.get("simd_level", "-"))
                timing_level = str(bcp_m.get("timing_level", "-"))

        rows.append([
            r["profile"],
            direction,
            strategy_or_method,
            simd_level,
            timing_level,
            f"{r['rows']:,}",
            f"{r['elapsed_s']:.2f}s",
            f"{r['rows_s']:,.0f}",
            f"{r['mb_s']:.2f}",
            f"{payload_mb:.1f}",
        ])

    headers = [
        "Profile", "Dir", "Strategy/Method", "SIMD", "Timing",
        "Rows", "Elapsed", "rows/s", "MB/s", "Payload MB"
    ]

    print()
    print(tabulate(rows, headers=headers, tablefmt="github"))

    # Cross-profile speedup relative to "simple" baseline per direction
    for dir_label in ("write", "load"):
        dir_results = [r for r in results if r.get("direction", "write") == dir_label]
        if len(dir_results) < 2:
            continue
        baselines = {}
        for r in dir_results:
            key = r.get("strategy", "cpp" if dir_label == "load" else "-")
            if key not in baselines:
                baselines[key] = r
        for r in dir_results:
            key = r.get("strategy", "cpp" if dir_label == "load" else "-")
            base = baselines.get(key)
            if base and base["profile"] != r["profile"] and base["rows_s"] > 0:
                ratio = r["rows_s"] / base["rows_s"]
                print(
                    f"  {r['profile']} vs {base['profile']} ({dir_label}/{key}): "
                    f"{ratio:.2f}x rows/s"
                )

    # Cross-strategy speedup per profile (write direction only)
    write_results = [r for r in results if r.get("direction", "write") == "write"]
    by_profile: dict[str, list[dict]] = {}
    for r in write_results:
        by_profile.setdefault(r["profile"], []).append(r)
    for pname, runs in by_profile.items():
        if len(runs) == 2:
            a, b = runs[0], runs[1]
            if a["rows_s"] > 0:
                ratio = b["rows_s"] / a["rows_s"]
                print(
                    f"  {pname}: {b['strategy']} vs {a['strategy']} = {ratio:.2f}x rows/s"
                )

    # Write vs load throughput per profile
    write_by_profile = {r["profile"]: r for r in results if r.get("direction") == "write"}
    load_by_profile = {r["profile"]: r for r in results if r.get("direction") == "load"}
    for pname in write_by_profile:
        if pname in load_by_profile:
            w = write_by_profile[pname]
            l = load_by_profile[pname]
            if l["rows_s"] > 0:
                ratio = w["rows_s"] / l["rows_s"]
                print(f"  {pname}: write/load ratio = {ratio:.2f}x rows/s")


# ── Regression gate ──────────────────────────────────────────────────────────

def _system_info() -> dict:
    """Capture machine identity for baseline provenance."""
    return {
        "os": platform.system(),
        "os_version": platform.version(),
        "machine": platform.machine(),
        "cpu_count": os.cpu_count(),
        "python": platform.python_version(),
        "node": platform.node(),
    }


def save_baseline(results: list[dict], args: argparse.Namespace, path: str) -> None:
    """Persist benchmark results as a JSON baseline file."""
    by_profile: dict[str, dict] = {}
    for r in results:
        direction = r.get("direction", "write")
        strategy_or_method = r.get("strategy", "cpp" if direction == "load" else "-")
        key = f"{r['profile']}/{direction}/{strategy_or_method}"
        by_profile[key] = {
            "profile": r["profile"],
            "direction": direction,
            "strategy": strategy_or_method,
            "mb_s": round(r["mb_s"], 2),
            "rows_s": round(r["rows_s"], 1),
            "elapsed_s": round(r["elapsed_s"], 3),
            "rows": r["rows"],
            "payload_bytes": r["payload_bytes"],
        }

    baseline = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "system": _system_info(),
        "config": {
            "rows": args.rows,
            "workers": args.workers,
            "batch_size": args.batch_size,
            "table_hint": args.table_hint,
            "strategy": args.strategy,
        },
        "results": by_profile,
    }

    with open(path, "w") as f:
        json.dump(baseline, f, indent=2)
    print(f"\nBaseline saved to {path} ({len(by_profile)} entries)")


def check_regression(
    results: list[dict],
    baseline_path: str,
    threshold_pct: float,
) -> bool:
    """Compare current results against a saved baseline.

    Returns True if all checks pass, False if any profile regressed
    beyond *threshold_pct* percent.
    """
    with open(baseline_path) as f:
        baseline = json.load(f)

    base_results = baseline["results"]
    base_sys = baseline.get("system", {})
    base_ts = baseline.get("timestamp", "?")

    print(f"\n{'=' * 72}")
    print(f"Regression check against baseline")
    print(f"  Baseline: {baseline_path}")
    print(f"  Created:  {base_ts}")
    print(f"  Machine:  {base_sys.get('node', '?')} ({base_sys.get('os', '?')})")
    print(f"  Threshold: {threshold_pct:.0f}%")
    print(f"{'=' * 72}")

    passed = True
    checked = 0
    for r in results:
        direction = r.get("direction", "write")
        strategy_or_method = r.get("strategy", "cpp" if direction == "load" else "-")
        key = f"{r['profile']}/{direction}/{strategy_or_method}"
        # Fallback to legacy key format for older baselines
        base = base_results.get(key) or base_results.get(
            f"{r['profile']}/{r.get('strategy', strategy_or_method)}")
        if base is None:
            print(f"  [{key:>20s}]  SKIP (no baseline entry)")
            continue

        checked += 1
        base_mb_s = float(base["mb_s"])
        curr_mb_s = r["mb_s"]

        if base_mb_s <= 0:
            print(f"  [{key:>20s}]  SKIP (baseline MB/s = 0)")
            continue

        change_pct = ((curr_mb_s - base_mb_s) / base_mb_s) * 100
        status = "PASS" if change_pct >= -threshold_pct else "FAIL"

        if status == "FAIL":
            passed = False

        sign = "+" if change_pct >= 0 else ""
        print(
            f"  [{key:>20s}]  {status}  "
            f"baseline={base_mb_s:7.2f} MB/s  "
            f"current={curr_mb_s:7.2f} MB/s  "
            f"({sign}{change_pct:.1f}%)"
        )

    print(f"{'=' * 72}")
    if checked == 0:
        print("  WARNING: No matching profiles found in baseline — nothing checked.")
        return True

    verdict = "PASSED" if passed else "FAILED"
    print(f"  Regression gate: {verdict}  ({checked} profile(s) checked)")
    print(f"{'=' * 72}")
    return passed


# ── CLI ──────────────────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="BCP throughput benchmark with simple / mixed / complex / exhaustive dataset profiles",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  %(prog)s --rows 500000 --dataset simple
  %(prog)s --rows 500000 --dataset all
  %(prog)s --rows 500000 --dataset all --compare-strategies
  %(prog)s --rows 500000 --dataset complex --strategy column_major

  # Write-only (no read-back)
  %(prog)s --rows 500000 --dataset simple --mode write

  # Skip verification but still read-back
  %(prog)s --rows 500000 --dataset simple --no-verify

Load (read) benchmark:
  # Load from table (must contain data)
  %(prog)s --dataset simple --mode load

  # Write then load in one pass
  %(prog)s --rows 500000 --dataset all --mode both

Regression gate:
  # Establish a baseline
  %(prog)s --rows 500000 --dataset all --save-baseline baseline.json

  # Check for regressions (exit 1 if >15%% drop)
  %(prog)s --rows 500000 --dataset all --check-regression baseline.json

  # Custom threshold (exit 1 if >10%% drop)
  %(prog)s --rows 500000 --dataset all --check-regression baseline.json --regression-threshold 10
""",
    )
    p.add_argument("--conn", default=None,
                   help="ODBC connection string (default: STRESS_CONN env var or local dev)")
    p.add_argument("--rows", type=int, default=1_000_000,
                   help="Number of rows to insert per run (default: 1,000,000)")
    p.add_argument("--dataset", choices=PROFILE_ORDER + ["all"], default="simple",
                   help="Dataset profile to test (default: simple)")
    p.add_argument("--mode", choices=["write", "load", "both"], default="both",
                   help="Benchmark mode: both (write + read-back + verify, default), "
                        "write (BCP insert only), or load (SELECT read only)")
    p.add_argument("--no-verify", action="store_true",
                   help="Skip round-trip data verification in 'both' mode")
    p.add_argument("--verify-sample", type=int, default=200,
                   help="Number of random rows to spot-check during verification (default: 200)")
    p.add_argument("--strategy", choices=["row_major", "column_major"], default="row_major",
                   help="Transpose strategy (default: row_major)")
    p.add_argument("--compare-strategies", action="store_true",
                   help="Run both row_major and column_major for each selected profile")
    p.add_argument("--batch-size", type=int, default=100_000,
                   help="BCP commit batch size (default: 100,000)")
    p.add_argument("--table-hint", default="TABLOCK",
                   help="SQL table hint (default: TABLOCK)")
    p.add_argument("--workers", type=int, default=0,
                   help="Number of parallel BCP connections (default: 0 = single-connection)")
    p.add_argument("--no-truncate", action="store_true",
                   help="Skip TRUNCATE before each run")
    p.add_argument("--warmup", action="store_true",
                   help="Run one warm-up pass (discarded) before timing")

    # Regression gate
    rg = p.add_argument_group("regression gate")
    rg.add_argument("--save-baseline", metavar="FILE",
                    help="Save results as a JSON baseline file")
    rg.add_argument("--check-regression", metavar="FILE",
                    help="Compare results against a saved baseline; exit 1 on regression")
    rg.add_argument("--regression-threshold", type=float, default=15.0,
                    metavar="PCT",
                    help="Max allowed throughput drop %% before failing (default: 15)")
    return p


def main() -> None:
    args = build_parser().parse_args()

    conn_str = args.conn or os.getenv("STRESS_CONN", "").strip() or default_connection_string()

    pl = _require("polars")
    pyodbc = _require("pyodbc")

    profiles = PROFILE_ORDER if args.dataset == "all" else [args.dataset]
    strategies = (
        ["row_major", "column_major"] if args.compare_strategies else [args.strategy]
    )

    # ── Ensure tables exist ──────────────────────────────────────────────────
    for pname in profiles:
        profile = PROFILES[pname]
        ensure_table(conn_str, profile["ddl"], pyodbc)

    # ── Generate data (only needed for write / both modes) ───────────────
    data: dict[str, object] = {}
    if args.mode in ("write", "both"):
        for pname in profiles:
            profile = PROFILES[pname]
            print(f"Generating {args.rows:,} rows for [{pname}] ({profile['columns']} cols) …")
            t0 = time.perf_counter()
            df = profile["generate"](args.rows, pl)
            gen_s = time.perf_counter() - t0
            size_mb = estimate_dataframe_size_bytes(df) / 1_048_576
            print(f"  -> {size_mb:.1f} MB in {gen_s:.2f}s")
            data[pname] = df

    # ── Warm-up ──────────────────────────────────────────────────────────────
    if args.warmup and args.mode in ("write", "both"):
        warmup_profile = profiles[0]
        warmup_table = PROFILES[warmup_profile]["table"]
        print(f"\nWarm-up pass ({warmup_profile}, {strategies[0]}) …")
        if not args.no_truncate:
            truncate_table(conn_str, warmup_table, pyodbc)
        run_one(conn_str, warmup_table, data[warmup_profile],
                strategies[0], args.batch_size, args.table_hint,
                args.workers)

    do_write = args.mode in ("write", "both")
    do_load  = args.mode in ("load", "both")

    do_verify = do_write and do_load and not args.no_verify

    # ── Write (BCP) benchmark runs ───────────────────────────────────────
    all_results: list[dict] = []
    verify_results: list[dict] = []
    if do_write:
        print()
        for pname in profiles:
            profile = PROFILES[pname]
            table = profile["table"]
            df = data[pname]
            for strat in strategies:
                if not args.no_truncate:
                    truncate_table(conn_str, table, pyodbc)
                print(f"Writing [{pname}] with {strat} …")
                r = run_one(conn_str, table, df, strat, args.batch_size, args.table_hint,
                            args.workers)
                r["profile"] = pname
                r["direction"] = "write"
                all_results.append(r)
                print_result(r, pname)

                # Verify immediately after write (before next strategy truncates)
                if do_verify:
                    v = verify_round_trip(
                        conn_str, table, df, pname, pyodbc,
                        sample_size=args.verify_sample,
                    )
                    v["strategy"] = strat
                    verify_results.append(v)
                    print_verify_result(v, pname)

    # ── Load (read) benchmark runs ───────────────────────────────────────
    if do_load:
        print()
        for pname in profiles:
            profile = PROFILES[pname]
            table = profile["table"]
            print(f"Loading [{pname}] …")
            r = run_load(conn_str, table, pname)
            all_results.append(r)
            print_load_result(r)

    # ── Verification summary ─────────────────────────────────────────────────
    if verify_results:
        all_passed = all(v["passed"] for v in verify_results)
        total_mismatches = sum(v["total_mismatches"] for v in verify_results)
        print()
        if all_passed:
            print(f"Round-trip verification: ALL PASSED  "
                  f"({len(verify_results)} run(s), 0 mismatches)")
        else:
            failed = [v for v in verify_results if not v["passed"]]
            print(f"Round-trip verification: {len(failed)} FAILED out of "
                  f"{len(verify_results)} run(s), {total_mismatches} total mismatches")

    # ── Summary ──────────────────────────────────────────────────────────────
    if len(all_results) > 1:
        print_comparison_table(all_results)

    # ── Regression gate ──────────────────────────────────────────────────────
    if args.save_baseline:
        save_baseline(all_results, args, args.save_baseline)

    if args.check_regression:
        ok = check_regression(all_results, args.check_regression,
                              args.regression_threshold)
        if not ok:
            sys.exit(1)

    # Exit non-zero if verification failed
    if verify_results and not all(v["passed"] for v in verify_results):
        sys.exit(2)


if __name__ == "__main__":
    main()
