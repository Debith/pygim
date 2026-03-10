#!/usr/bin/env python3
"""
BCP throughput benchmark with multiple dataset profiles.

Runs identical BCP insert logic against three table schemas so you can
compare apples-to-apples across simple, mixed, and complex column sets.

Profiles:
  simple     – 7 columns  (INT, BIGINT, FLOAT, NVARCHAR, DATE, DATETIME2)
  mixed      – 9 columns  (adds DECIMAL, wider NVARCHAR)
  complex    – 11 columns (adds BIT, DECIMAL, NCHAR, UNIQUEIDENTIFIER)
  exhaustive – 19 columns (every supported Arrow type: INT8-64, UINT8-64,
                            BOOL, FLOAT32/64, DATE, TIME, DATETIME2,
                            DURATION, NVARCHAR, VARBINARY)

Usage:
  # Single profile, single strategy
  python benchmarks/bcp_throughput.py --rows 500000 --dataset simple

  # Compare row_major vs column_major for one profile
  python benchmarks/bcp_throughput.py --rows 500000 --dataset mixed --compare-strategies

  # Run ALL profiles side-by-side
  python benchmarks/bcp_throughput.py --rows 500000 --dataset all

  # Full matrix: every profile × both strategies
  python benchmarks/bcp_throughput.py --rows 500000 --dataset all --compare-strategies

Connection:
  Set STRESS_CONN env var, or pass --conn, or let it fall back to the
  local dev default.
"""
from __future__ import annotations

import argparse
import os
import sys
import time

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
    bcp = r["bcp_metrics"]
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


def print_comparison_table(results: list[dict]) -> None:
    """Print a compact comparison table across all runs."""
    from tabulate import tabulate

    rows = []
    for r in results:
        payload_mb = r["payload_bytes"] / 1_048_576
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
            r["strategy"],
            simd_level,
            timing_level,
            f"{r['rows']:,}",
            f"{r['elapsed_s']:.2f}s",
            f"{r['rows_s']:,.0f}",
            f"{r['mb_s']:.2f}",
            f"{payload_mb:.1f}",
        ])

    headers = [
        "Profile", "Strategy", "SIMD", "Timing",
        "Rows", "Elapsed", "rows/s", "MB/s", "Payload MB"
    ]

    print()
    print(tabulate(rows, headers=headers, tablefmt="github"))

    # Cross-profile speedup relative to "simple" baseline (first strategy encountered)
    baselines = {}
    for r in results:
        key = r["strategy"]
        if key not in baselines:
            baselines[key] = r
    for r in results:
        base = baselines.get(r["strategy"])
        if base and base["profile"] != r["profile"] and base["rows_s"] > 0:
            ratio = r["rows_s"] / base["rows_s"]
            print(
                f"  {r['profile']} vs {base['profile']} ({r['strategy']}): "
                f"{ratio:.2f}x rows/s"
            )

    # Cross-strategy speedup per profile
    by_profile: dict[str, list[dict]] = {}
    for r in results:
        by_profile.setdefault(r["profile"], []).append(r)
    for pname, runs in by_profile.items():
        if len(runs) == 2:
            a, b = runs[0], runs[1]
            if a["rows_s"] > 0:
                ratio = b["rows_s"] / a["rows_s"]
                print(
                    f"  {pname}: {b['strategy']} vs {a['strategy']} = {ratio:.2f}x rows/s"
                )


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
""",
    )
    p.add_argument("--conn", default=None,
                   help="ODBC connection string (default: STRESS_CONN env var or local dev)")
    p.add_argument("--rows", type=int, default=1_000_000,
                   help="Number of rows to insert per run (default: 1,000,000)")
    p.add_argument("--dataset", choices=PROFILE_ORDER + ["all"], default="simple",
                   help="Dataset profile to test (default: simple)")
    p.add_argument("--strategy", choices=["row_major", "column_major"], default="row_major",
                   help="Transpose strategy (default: row_major)")
    p.add_argument("--compare-strategies", action="store_true",
                   help="Run both row_major and column_major for each selected profile")
    p.add_argument("--batch-size", type=int, default=100_000,
                   help="BCP commit batch size (default: 100,000)")
    p.add_argument("--table-hint", default="TABLOCK",
                   help="SQL table hint (default: TABLOCK)")
    p.add_argument("--no-truncate", action="store_true",
                   help="Skip TRUNCATE before each run")
    p.add_argument("--warmup", action="store_true",
                   help="Run one warm-up pass (discarded) before timing")
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

    # ── Generate data ────────────────────────────────────────────────────────
    data: dict[str, object] = {}
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
    if args.warmup:
        warmup_profile = profiles[0]
        warmup_table = PROFILES[warmup_profile]["table"]
        print(f"\nWarm-up pass ({warmup_profile}, {strategies[0]}) …")
        if not args.no_truncate:
            truncate_table(conn_str, warmup_table, pyodbc)
        run_one(conn_str, warmup_table, data[warmup_profile],
                strategies[0], args.batch_size, args.table_hint)

    # ── Benchmark runs ───────────────────────────────────────────────────────
    all_results: list[dict] = []
    print()
    for pname in profiles:
        profile = PROFILES[pname]
        table = profile["table"]
        df = data[pname]
        for strat in strategies:
            if not args.no_truncate:
                truncate_table(conn_str, table, pyodbc)
            print(f"Running [{pname}] with {strat} …")
            r = run_one(conn_str, table, df, strat, args.batch_size, args.table_hint)
            r["profile"] = pname
            all_results.append(r)
            print_result(r, pname)

    # ── Summary ──────────────────────────────────────────────────────────────
    if len(all_results) > 1:
        print_comparison_table(all_results)


if __name__ == "__main__":
    main()
