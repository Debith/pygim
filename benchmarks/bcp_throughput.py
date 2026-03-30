#!/usr/bin/env python3
"""
BCP throughput benchmark — target architecture (placeholder).

Exercises the full Repository pipeline:
  acquire_repo → save (Polars → Arrow → BCP) → load (ODBC → Arrow → Polars)

Uses the new target architecture API:
  from pygim.repository import acquire_repo, Query

Profiles:
  simple     – 7 columns  (INT, BIGINT, FLOAT, NVARCHAR, DATE, DATETIME2)
  mixed      – 9 columns  (adds DECIMAL, wider NVARCHAR)
  complex    – 11 columns (adds BIT, DECIMAL, NCHAR, UNIQUEIDENTIFIER)
  exhaustive – 19 columns (every supported Arrow type)

Modes:
  both  – write + read-back + verify (default)
  write – BCP insert only
  load  – SELECT read only (table must already contain data)
"""
from __future__ import annotations

import json
import math
import os
import platform
import sys
import time
from datetime import date as py_date
from datetime import datetime, timezone
from decimal import Decimal

import click

print(f"PID: {os.getpid()}")

# ── Dependency helpers ───────────────────────────────────────────────────────

def _require(mod: str):
    import importlib
    try:
        return importlib.import_module(mod)
    except ModuleNotFoundError:
        click.echo(f"ERROR: '{mod}' is required.  pip install {mod}", err=True)
        sys.exit(1)


def default_connection_string() -> str:
    pwd = os.getenv("MSSQL_SA_PASSWORD", "NewP@ssw0rd#2025")
    return (
        f"Driver={{ODBC Driver 18 for SQL Server}};Server=localhost,1433;"
        f"Database=master;UID=sa;PWD={pwd};"
        f"Encrypt=yes;TrustServerCertificate=yes;"
    )


# ── Dataset schemas (for pygim.create_df C++ data generator) ─────────────────

import pygim

_SCHEMA_SIMPLE = {
    "id": "serial", "val_i32": "int32", "val_i64": "int64",
    "val_f64": "float64", "val_str": "string",
    "val_date": "date", "val_ts": "timestamp",
}

_SCHEMA_MIXED = {
    "id": "serial", "val_i32": "int32", "val_i64": "int64",
    "val_f64": "float64", "val_dec": "float64",
    "val_str": "string", "val_text": "string",
    "val_date": "date", "val_ts": "timestamp",
}

_SCHEMA_COMPLEX = {
    "id": "serial", "col_int": "int32", "col_bigint": "int64",
    "col_bit": "bool", "col_decimal": "float64", "col_float": "float64",
    "col_nvarchar": "string", "col_nchar": "string",
    "col_date": "date", "col_datetime2": "timestamp", "col_uuid": "uuid",
}

_SCHEMA_EXHAUSTIVE = {
    "id": "serial",
    "col_int8": "int8", "col_int16": "int16", "col_int32": "int32",
    "col_int64": "int64", "col_uint8": "uint8", "col_uint16": "uint16",
    "col_uint32": "uint32", "col_uint64": "uint64",
    "col_bool": "bool", "col_float32": "float32", "col_float64": "float64",
    "col_date": "date", "col_time": "time", "col_datetime2": "timestamp",
    "col_duration_us": "duration", "col_nvarchar": "string",
    "col_binary": "binary",
}


# ── Dataset profiles ─────────────────────────────────────────────────────────

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
        "schema": _SCHEMA_SIMPLE,
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
        "schema": _SCHEMA_MIXED,
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
        "schema": _SCHEMA_COMPLEX,
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
        "schema": _SCHEMA_EXHAUSTIVE,
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


# ── Payload estimation ───────────────────────────────────────────────────────

def estimate_size_bytes(frame) -> int:
    for attr in ("estimated_size", "estimated_size_bytes"):
        fn = getattr(frame, attr, None)
        if callable(fn):
            try:
                return max(int(fn()), 0)
            except Exception:
                continue
    return 0


# ── Benchmark runners ────────────────────────────────────────────────────────

def run_write(
    conn_str: str,
    table: str,
    df,
    profile_name: str,
    bcp_workers: int,
    batch_size: int = 100_000,
) -> dict:
    """Insert *df* via BCP and return timing metrics."""
    from pygim.repository import acquire_repo

    repo = acquire_repo(conn_str, format="polars",
                        batch_size=batch_size,
                        bcp_workers=bcp_workers)

    payload_bytes = estimate_size_bytes(df)
    nrows = len(df)

    t0 = time.perf_counter()
    metrics = repo.save(df, table)
    elapsed = time.perf_counter() - t0

    mb_s = (payload_bytes / 1_048_576) / elapsed if elapsed > 0 else 0.0

    return {
        "profile":       profile_name,
        "direction":     "write",
        "rows":          nrows,
        "elapsed_s":     elapsed,
        "payload_bytes": payload_bytes,
        "mb_s":          mb_s,
        "rows_s":        nrows / elapsed if elapsed > 0 else 0.0,
        "bcp_workers":   bcp_workers,
        "bcp_metrics":   metrics,
    }


def run_load(
    conn_str: str,
    table: str,
    profile_name: str,
    load_workers: int,
) -> dict:
    """Load all rows from *table* via Repository.load() (Arrow → Polars)."""
    from pygim.repository import acquire_repo

    repo = acquire_repo(conn_str, format="polars")

    t0 = time.perf_counter()
    repo.load(table, load_workers=load_workers)
    elapsed = time.perf_counter() - t0

    # Placeholder: real code would measure actual returned DataFrame
    nrows = 0
    payload_bytes = 0
    mb_s = 0.0

    return {
        "profile":       profile_name,
        "direction":     "load",
        "rows":          nrows,
        "elapsed_s":     elapsed,
        "payload_bytes": payload_bytes,
        "mb_s":          mb_s,
        "rows_s":        nrows / elapsed if elapsed > 0 else 0.0,
        "load_workers":  load_workers,
    }


# ── Round-trip verification ───────────────────────────────────────────────────

def _polars_row_to_pydict(df, idx: int) -> dict:
    return {col: df[col][idx] for col in df.columns}


def _values_match(source, actual, col_name: str, *, float_tol: float = 1e-6) -> bool:
    """Compare a single cell value — handles Arrow → BCP → SQL → pyodbc coercions."""
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
        src_us = (source.days * 86_400_000_000
                  + source.seconds * 1_000_000
                  + source.microseconds)
        if isinstance(actual, int):
            return src_us == actual
        if isinstance(actual, _dt.timedelta):
            return source == actual
        return str(source) == str(actual)

    # Integer — handle signed↔unsigned wrapping (int8 → TINYINT)
    if isinstance(source, int) and isinstance(actual, int):
        if source == actual:
            return True
        if -128 <= source < 0 and 0 <= actual <= 255:
            return (source & 0xFF) == actual
        if -32768 <= source < 0 and 0 <= actual <= 65535:
            return (source & 0xFFFF) == actual
        return False

    if isinstance(source, int) and isinstance(actual, float):
        return float(source) == actual
    if isinstance(source, float) and isinstance(actual, int):
        return source == float(actual)

    # Float — relative tolerance; extra slack for SQL DECIMAL truncation
    if isinstance(source, float) and isinstance(actual, (float, int, Decimal)):
        a, b = float(source), float(actual)
        if math.isnan(a) and math.isnan(b):
            return True
        if a == 0 and b == 0:
            return True
        tol = max(float_tol, 5e-5) if isinstance(actual, Decimal) else float_tol
        return abs(a - b) <= tol * max(abs(a), abs(b), 1.0)

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
        if s.lower() == a.lower():
            return True
        return False

    # Date
    if isinstance(source, py_date) and not isinstance(source, datetime):
        if isinstance(actual, py_date):
            return source == (actual.date() if isinstance(actual, datetime) else actual)

    # Datetime (1ms tolerance for DATETIME2(6))
    if isinstance(source, datetime) and isinstance(actual, datetime):
        return abs((source - actual).total_seconds()) < 0.001

    # bytes / binary
    if isinstance(source, (bytes, bytearray)) and isinstance(actual, (bytes, bytearray)):
        return bytes(source) == bytes(actual)

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
    """Read back data and verify cell-by-cell against *source_df*."""
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
    sample_ids = sorted(random.sample(
        range(1, src_count + 1), min(sample_size, src_count)))
    id_list = ",".join(str(i) for i in sample_ids)
    cursor.execute(f"SELECT * FROM {table} WHERE id IN ({id_list}) ORDER BY id")
    db_rows = cursor.fetchall()
    cursor.close()
    cn.close()

    mismatches = []
    rows_checked = 0
    for db_row in db_rows:
        row_id = db_row[0]
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
        "passed":           passed,
        "row_count_ok":     count_ok,
        "src_rows":         src_count,
        "db_rows":          db_count,
        "cols_ok":          cols_ok,
        "src_cols":         src_cols,
        "db_cols":          db_cols,
        "rows_checked":     rows_checked,
        "mismatches":       mismatches[:20],
        "total_mismatches": len(mismatches),
    }


# ── Reporting ────────────────────────────────────────────────────────────────

def print_write_result(r: dict) -> None:
    payload_mb = r["payload_bytes"] / 1_048_576
    click.echo(
        f"  [{r['profile']:>11s}]  "
        f"rows={r['rows']:>10,}  "
        f"elapsed={r['elapsed_s']:6.2f}s  "
        f"rows/s={r['rows_s']:>12,.0f}  "
        f"MB/s={r['mb_s']:7.2f}  "
        f"payload={payload_mb:6.1f} MB  "
        f"workers={r['bcp_workers']}"
    )
    bcp = r.get("bcp_metrics")
    if bcp:
        click.echo(
            f"           C++: total={bcp['total_seconds']:.2f}s  "
            f"row_loop={bcp['row_loop_seconds']:.2f}s  "
            f"flush={bcp['batch_flush_seconds']:.2f}s  "
            f"batches={bcp['record_batches']}"
        )


def print_load_result(r: dict) -> None:
    payload_mb = r["payload_bytes"] / 1_048_576
    click.echo(
        f"  [{r['profile']:>11s}]  "
        f"rows={r['rows']:>10,}  "
        f"elapsed={r['elapsed_s']:6.2f}s  "
        f"rows/s={r['rows_s']:>12,.0f}  "
        f"MB/s={r['mb_s']:7.2f}  "
        f"payload={payload_mb:6.1f} MB  "
        f"workers={r['load_workers']}"
    )


def print_verify_result(v: dict, profile: str) -> None:
    status = "PASS" if v["passed"] else "FAIL"
    parts = [
        f"  [{profile:>11s}]  verify={status}",
        f"rows={v['src_rows']:,}/{v['db_rows']:,}",
        f"cols={'OK' if v['cols_ok'] else 'MISMATCH'}",
        f"sample={v['rows_checked']:,} checked",
    ]
    if v["total_mismatches"] > 0:
        parts.append(f"mismatches={v['total_mismatches']}")
    click.echo("  ".join(parts))

    if not v["passed"]:
        if not v["row_count_ok"]:
            click.echo(f"           Row count: source={v['src_rows']:,}  "
                       f"db={v['db_rows']:,}")
        if not v["cols_ok"]:
            click.echo(f"           Columns: source={v['src_cols']}")
            click.echo(f"                    db    ={v['db_cols']}")
        for m in v["mismatches"][:10]:
            if "column" in m:
                click.echo(
                    f"           id={m['id']}  col={m['column']}  "
                    f"source={m['source']} ({m['source_type']})  "
                    f"actual={m['actual']} ({m['actual_type']})"
                )
            else:
                click.echo(f"           id={m['id']}  {m.get('error', '?')}")
        if v["total_mismatches"] > 10:
            click.echo(f"           … and {v['total_mismatches'] - 10} more")


def print_comparison_table(results: list[dict]) -> None:
    from tabulate import tabulate

    rows = []
    for r in results:
        payload_mb = r["payload_bytes"] / 1_048_576
        direction = r.get("direction", "write")
        workers = r.get("bcp_workers", r.get("load_workers", 0))
        rows.append([
            r["profile"],
            direction,
            workers,
            f"{r['rows']:,}",
            f"{r['elapsed_s']:.2f}s",
            f"{r['rows_s']:,.0f}",
            f"{r['mb_s']:.2f}",
            f"{payload_mb:.1f}",
        ])

    headers = [
        "Profile", "Dir", "Workers", "Rows",
        "Elapsed", "rows/s", "MB/s", "Payload MB",
    ]
    click.echo()
    click.echo(tabulate(rows, headers=headers, tablefmt="github"))

    # Cross-profile speedup relative to first profile per direction
    for dir_label in ("write", "load"):
        dir_results = [r for r in results if r.get("direction") == dir_label]
        if len(dir_results) < 2:
            continue
        base = dir_results[0]
        for r in dir_results[1:]:
            if base["rows_s"] > 0:
                ratio = r["rows_s"] / base["rows_s"]
                click.echo(
                    f"  {r['profile']} vs {base['profile']} ({dir_label}): "
                    f"{ratio:.2f}x rows/s"
                )

    # Write vs load per profile
    write_by = {r["profile"]: r for r in results if r.get("direction") == "write"}
    load_by = {r["profile"]: r for r in results if r.get("direction") == "load"}
    for pname in write_by:
        if pname in load_by and load_by[pname]["rows_s"] > 0:
            ratio = write_by[pname]["rows_s"] / load_by[pname]["rows_s"]
            click.echo(f"  {pname}: write/load ratio = {ratio:.2f}x rows/s")


# ── Regression gate ──────────────────────────────────────────────────────────

def _system_info() -> dict:
    return {
        "os": platform.system(),
        "os_version": platform.version(),
        "machine": platform.machine(),
        "cpu_count": os.cpu_count(),
        "python": platform.python_version(),
        "node": platform.node(),
    }


def save_baseline(results: list[dict], rows: int, workers: int,
                   path: str) -> None:
    by_key: dict[str, dict] = {}
    for r in results:
        direction = r.get("direction", "write")
        w = r.get("bcp_workers", r.get("load_workers", 0))
        key = f"{r['profile']}/{direction}/w{w}"
        by_key[key] = {
            "profile": r["profile"],
            "direction": direction,
            "workers": w,
            "mb_s": round(r["mb_s"], 2),
            "rows_s": round(r["rows_s"], 1),
            "elapsed_s": round(r["elapsed_s"], 3),
            "rows": r["rows"],
            "payload_bytes": r["payload_bytes"],
        }

    baseline = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "system": _system_info(),
        "config": {"rows": rows, "workers": workers},
        "results": by_key,
    }
    with open(path, "w") as f:
        json.dump(baseline, f, indent=2)
    click.echo(f"\nBaseline saved to {path} ({len(by_key)} entries)")


def check_regression(
    results: list[dict],
    baseline_path: str,
    threshold_pct: float,
) -> bool:
    with open(baseline_path) as f:
        baseline = json.load(f)

    base_results = baseline["results"]
    base_sys = baseline.get("system", {})
    base_ts = baseline.get("timestamp", "?")

    click.echo(f"\n{'=' * 72}")
    click.echo(f"Regression check against baseline")
    click.echo(f"  Baseline: {baseline_path}")
    click.echo(f"  Created:  {base_ts}")
    click.echo(f"  Machine:  {base_sys.get('node', '?')} "
               f"({base_sys.get('os', '?')})")
    click.echo(f"  Threshold: {threshold_pct:.0f}%")
    click.echo(f"{'=' * 72}")

    passed = True
    checked = 0
    for r in results:
        direction = r.get("direction", "write")
        w = r.get("bcp_workers", r.get("load_workers", 0))
        key = f"{r['profile']}/{direction}/w{w}"
        base = base_results.get(key)
        if base is None:
            click.echo(f"  [{key:>30s}]  SKIP (no baseline)")
            continue

        checked += 1
        base_mb_s = float(base["mb_s"])
        curr_mb_s = r["mb_s"]

        if base_mb_s <= 0:
            click.echo(f"  [{key:>30s}]  SKIP (baseline MB/s = 0)")
            continue

        change_pct = ((curr_mb_s - base_mb_s) / base_mb_s) * 100
        status = "PASS" if change_pct >= -threshold_pct else "FAIL"
        if status == "FAIL":
            passed = False

        sign = "+" if change_pct >= 0 else ""
        click.echo(
            f"  [{key:>30s}]  {status}  "
            f"baseline={base_mb_s:7.2f}  "
            f"current={curr_mb_s:7.2f} MB/s  "
            f"({sign}{change_pct:.1f}%)"
        )

    click.echo(f"{'=' * 72}")
    if checked == 0:
        click.echo("  WARNING: No matching profiles — nothing checked.")
        return True

    verdict = "PASSED" if passed else "FAILED"
    click.echo(f"  Regression gate: {verdict}  ({checked} profile(s) checked)")
    click.echo(f"{'=' * 72}")
    return passed


# ── CLI ──────────────────────────────────────────────────────────────────────

@click.command()
@click.option("--conn", default=None,
              help="ODBC connection string (default: STRESS_CONN env or local dev)")
@click.option("--rows", default=1_000_000, type=int, show_default=True,
              help="Rows to insert per run")
@click.option("--dataset",
              type=click.Choice(PROFILE_ORDER + ["all"], case_sensitive=False),
              default="simple", show_default=True,
              help="Dataset profile to test")
@click.option("--mode",
              type=click.Choice(["write", "load", "both"], case_sensitive=False),
              default="both", show_default=True,
              help="Benchmark mode")
@click.option("--format", "fmt",
              type=click.Choice(["polars", "pandas"], case_sensitive=False),
              default="polars", show_default=True,
              help="Output data format")
@click.option("--workers", default=0, type=int, show_default=True,
              help="Parallel BCP / load connections (0 = single)")
@click.option("--no-verify", is_flag=True,
              help="Skip round-trip verification in 'both' mode")
@click.option("--verify-sample", default=200, type=int, show_default=True,
              help="Rows to spot-check during verification")
@click.option("--batch-size", default=100_000, type=int, show_default=True,
              help="BCP batch size (rows per bcp_batch call)")
@click.option("--no-truncate", is_flag=True,
              help="Skip TRUNCATE before each write run")
@click.option("--warmup", is_flag=True,
              help="Run one discarded warm-up pass before timing")
@click.option("--save-baseline", "baseline_out", type=click.Path(), default=None,
              help="Save results as JSON baseline file")
@click.option("--check-regression", "baseline_in", type=click.Path(exists=True),
              default=None,
              help="Compare against saved baseline; exit 1 on regression")
@click.option("--regression-threshold", default=15.0, type=float,
              show_default=True, help="Max throughput drop % before failing")
def bench(
    conn, rows, dataset, mode, fmt, workers,
    no_verify, verify_sample, batch_size, no_truncate, warmup,
    baseline_out, baseline_in, regression_threshold,
):
    """BCP throughput benchmark — target architecture (placeholder)."""
    conn_str = conn or os.getenv("STRESS_CONN", "").strip() or default_connection_string()

    pl = _require("polars")
    pyodbc = _require("pyodbc")

    profiles = PROFILE_ORDER if dataset == "all" else [dataset]

    # ── Ensure tables ────────────────────────────────────────────────────
    for pname in profiles:
        ensure_table(conn_str, PROFILES[pname]["ddl"], pyodbc)

    # ── Generate data ────────────────────────────────────────────────────
    data: dict[str, object] = {}
    if mode in ("write", "both"):
        for pname in profiles:
            profile = PROFILES[pname]
            click.echo(f"Generating {rows:,} rows for [{pname}] "
                       f"({profile['columns']} cols) …")
            t0 = time.perf_counter()
            df = pygim.create_df(profile["schema"], rows=rows)
            gen_s = time.perf_counter() - t0
            size_mb = estimate_size_bytes(df) / 1_048_576
            click.echo(f"  → {size_mb:.1f} MB in {gen_s:.2f}s")
            data[pname] = df

    # ── Warm-up ──────────────────────────────────────────────────────────
    if warmup and mode in ("write", "both"):
        pname0 = profiles[0]
        click.echo(f"\nWarm-up pass ({pname0}) …")
        if not no_truncate:
            truncate_table(conn_str, PROFILES[pname0]["table"], pyodbc)
        run_write(conn_str, PROFILES[pname0]["table"], data[pname0],
                  pname0, workers, batch_size=batch_size)

    do_write = mode in ("write", "both")
    do_load = mode in ("load", "both")
    do_verify = do_write and do_load and not no_verify

    # ── Write runs ───────────────────────────────────────────────────────
    all_results: list[dict] = []
    verify_results: list[dict] = []

    if do_write:
        click.echo()
        for pname in profiles:
            table = PROFILES[pname]["table"]
            df = data[pname]

            if not no_truncate:
                truncate_table(conn_str, table, pyodbc)

            click.echo(f"Writing [{pname}] …")
            r = run_write(conn_str, table, df, pname, workers, batch_size=batch_size)
            all_results.append(r)
            print_write_result(r)

            if do_verify:
                v = verify_round_trip(
                    conn_str, table, df, pname, pyodbc,
                    sample_size=verify_sample,
                )
                verify_results.append(v)
                print_verify_result(v, pname)

    # ── Load runs ────────────────────────────────────────────────────────
    if do_load:
        click.echo()
        for pname in profiles:
            table = PROFILES[pname]["table"]
            click.echo(f"Loading [{pname}] …")
            r = run_load(conn_str, table, pname, workers)
            all_results.append(r)
            print_load_result(r)

    # ── Verification summary ─────────────────────────────────────────────
    if verify_results:
        all_passed = all(v["passed"] for v in verify_results)
        total_mm = sum(v["total_mismatches"] for v in verify_results)
        click.echo()
        if all_passed:
            click.echo(f"Round-trip verification: ALL PASSED  "
                       f"({len(verify_results)} run(s), 0 mismatches)")
        else:
            failed = sum(1 for v in verify_results if not v["passed"])
            click.echo(f"Round-trip verification: {failed} FAILED / "
                       f"{len(verify_results)} run(s), "
                       f"{total_mm} total mismatches")

    # ── Summary table ────────────────────────────────────────────────────
    if len(all_results) > 1:
        print_comparison_table(all_results)

    # ── Regression gate ──────────────────────────────────────────────────
    if baseline_out:
        save_baseline(all_results, rows, workers, baseline_out)

    if baseline_in:
        ok = check_regression(
            all_results, baseline_in, regression_threshold)
        if not ok:
            sys.exit(1)

    # Exit non-zero if verification failed
    if verify_results and not all(v["passed"] for v in verify_results):
        sys.exit(2)


if __name__ == "__main__":
    bench()
