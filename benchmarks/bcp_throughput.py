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
import datetime
import os
import sys
import time
import uuid as _uuid

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

_EPOCH = datetime.date(1970, 1, 1)
_TS_BASE = datetime.datetime(2020, 1, 1)


def _generate_simple(n: int, pl) -> object:
    """7 columns: id, INT, BIGINT, FLOAT, NVARCHAR(64), DATE, DATETIME2(6)."""
    return pl.DataFrame({
        "id":       list(range(1, n + 1)),
        "val_i32":  [i % 100_000 for i in range(n)],
        "val_i64":  [i * 10_000_000_000 for i in range(n)],
        "val_f64":  [float(i) * 1.23456789 for i in range(n)],
        "val_str":  [f"row_{i:07d}" for i in range(n)],
        "val_date": [_EPOCH + datetime.timedelta(days=i % 10_000) for i in range(n)],
        "val_ts":   [_TS_BASE + datetime.timedelta(microseconds=i * 1_000_003) for i in range(n)],
    })


def _generate_mixed(n: int, pl) -> object:
    """9 columns: simple + DECIMAL(18,4), wider NVARCHAR(200)."""
    return pl.DataFrame({
        "id":        list(range(1, n + 1)),
        "val_i32":   [i % 100_000 for i in range(n)],
        "val_i64":   [i * 10_000_000_000 for i in range(n)],
        "val_f64":   [float(i) * 1.23456789 for i in range(n)],
        "val_dec":   [round(float(i) * 0.1234, 4) for i in range(n)],
        "val_str":   [f"row_{i:07d}" for i in range(n)],
        "val_text":  [f"description for item number {i:09d} in the benchmark dataset" for i in range(n)],
        "val_date":  [_EPOCH + datetime.timedelta(days=i % 10_000) for i in range(n)],
        "val_ts":    [_TS_BASE + datetime.timedelta(microseconds=i * 1_000_003) for i in range(n)],
    })


def _generate_complex(n: int, pl) -> object:
    """11 columns: INT PK + INT, BIGINT, BIT, DECIMAL, FLOAT, NVARCHAR, NCHAR, DATE, DATETIME2, UUID."""
    return pl.DataFrame({
        "id":             list(range(1, n + 1)),
        "col_int":        [i % 100_000 for i in range(n)],
        "col_bigint":     [i * 10_000_000_000 for i in range(n)],
        "col_bit":        [i % 2 == 0 for i in range(n)],
        "col_decimal":    [round(float(i) * 0.1234, 4) for i in range(n)],
        "col_float":      [float(i) * 1.23456789 for i in range(n)],
        "col_nvarchar":   [f"row_{i:07d}" for i in range(n)],
        "col_nchar":      [f"NC{i % 10_000:06d}" for i in range(n)],
        "col_date":       [_EPOCH + datetime.timedelta(days=i % 10_000) for i in range(n)],
        "col_datetime2":  [_TS_BASE + datetime.timedelta(microseconds=i * 1_000_003) for i in range(n)],
        "col_uuid":       [str(_uuid.uuid5(_uuid.NAMESPACE_DNS, f"bench-{i}")) for i in range(n)],
    })


def _generate_exhaustive(n: int, pl) -> object:
    """18 columns: exercises every Arrow type dispatch path in the BCP binder.

    Arrow types covered: INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32,
    UINT64, BOOL, FLOAT, DOUBLE, DATE32, TIME64, TIMESTAMP, DURATION,
    STRING (via LARGE_STRING), LARGE_BINARY.

    Note: unsigned integers are kept within signed SQL Server type ranges
    (INT8 >= 0 for TINYINT, UINT16 < 32768, UINT32 < 2^31, UINT64 < 2^63)
    to avoid overflow in the BCP int1/int2/int4/bigint mappings.
    """
    return pl.DataFrame({
        "id":              pl.Series("id", list(range(1, n + 1)), dtype=pl.Int32),
        # ── Signed integers ──────────────────────────────────────────────
        "col_int8":        pl.Series("col_int8", [i % 120 for i in range(n)], dtype=pl.Int8),
        "col_int16":       pl.Series("col_int16", [i % 30_000 - 15_000 for i in range(n)], dtype=pl.Int16),
        "col_int32":       pl.Series("col_int32", [i % 100_000 for i in range(n)], dtype=pl.Int32),
        "col_int64":       pl.Series("col_int64", [i * 10_000_000 for i in range(n)], dtype=pl.Int64),
        # ── Unsigned integers (values kept within signed SQL target range) ──
        "col_uint8":       pl.Series("col_uint8", [i % 250 for i in range(n)], dtype=pl.UInt8),
        "col_uint16":      pl.Series("col_uint16", [i % 30_000 for i in range(n)], dtype=pl.UInt16),
        "col_uint32":      pl.Series("col_uint32", [i % 2_000_000_000 for i in range(n)], dtype=pl.UInt32),
        "col_uint64":      pl.Series("col_uint64", [i % 4_000_000_000 for i in range(n)], dtype=pl.UInt64),
        # ── Boolean ──────────────────────────────────────────────────────
        "col_bool":        pl.Series("col_bool", [i % 2 == 0 for i in range(n)], dtype=pl.Boolean),
        # ── Floating-point ───────────────────────────────────────────────
        "col_float32":     pl.Series("col_float32", [float(i) * 0.5 for i in range(n)], dtype=pl.Float32),
        "col_float64":     pl.Series("col_float64", [float(i) * 1.23456789 for i in range(n)], dtype=pl.Float64),
        # ── Temporal ─────────────────────────────────────────────────────
        "col_date":        [_EPOCH + datetime.timedelta(days=i % 10_000) for i in range(n)],
        "col_time":        pl.Series("col_time",
                                     [datetime.time(hour=i % 24, minute=(i * 7) % 60, second=(i * 13) % 60)
                                      for i in range(n)],
                                     dtype=pl.Time),
        "col_datetime2":   [_TS_BASE + datetime.timedelta(microseconds=i * 1_000_003) for i in range(n)],
        "col_duration_us": pl.Series("col_duration_us",
                                     [datetime.timedelta(microseconds=i * 1000) for i in range(n)],
                                     dtype=pl.Duration("us")),
        # ── String / binary ──────────────────────────────────────────────
        "col_nvarchar":    [f"row_{i:07d}" for i in range(n)],
        "col_binary":      pl.Series("col_binary",
                                     [bytes([i % 256] * (1 + i % 16)) for i in range(n)],
                                     dtype=pl.Binary),
    })


PROFILES: dict[str, dict] = {
    "simple": {
        "table": "dbo.bcp_bench_simple",
        "ddl": """
IF OBJECT_ID(N'dbo.bcp_bench_simple', N'U') IS NULL
CREATE TABLE dbo.bcp_bench_simple (
    id       INT          NOT NULL PRIMARY KEY,
    val_i32  INT          NOT NULL,
    val_i64  BIGINT       NOT NULL,
    val_f64  FLOAT        NOT NULL,
    val_str  NVARCHAR(64) NOT NULL,
    val_date DATE         NOT NULL,
    val_ts   DATETIME2(6) NOT NULL
);""",
        "generate": _generate_simple,
        "columns": 7,
    },
    "mixed": {
        "table": "dbo.bcp_bench_mixed",
        "ddl": """
IF OBJECT_ID(N'dbo.bcp_bench_mixed', N'U') IS NULL
CREATE TABLE dbo.bcp_bench_mixed (
    id        INT            NOT NULL PRIMARY KEY,
    val_i32   INT            NOT NULL,
    val_i64   BIGINT         NOT NULL,
    val_f64   FLOAT          NOT NULL,
    val_dec   DECIMAL(18,4)  NOT NULL,
    val_str   NVARCHAR(64)   NOT NULL,
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
IF OBJECT_ID(N'dbo.bcp_bench_complex', N'U') IS NULL
CREATE TABLE dbo.bcp_bench_complex (
    id             INT                NOT NULL PRIMARY KEY,
    col_int        INT                NOT NULL,
    col_bigint     BIGINT             NOT NULL,
    col_bit        BIT                NOT NULL,
    col_decimal    DECIMAL(18,4)      NOT NULL,
    col_float      FLOAT              NOT NULL,
    col_nvarchar   NVARCHAR(100)      NOT NULL,
    col_nchar      NCHAR(10)          NOT NULL,
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
IF OBJECT_ID(N'dbo.bcp_bench_exhaustive', N'U') IS NULL
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
