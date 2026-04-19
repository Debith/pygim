#!/usr/bin/env python3
"""Experiment: three approaches to improve parallel timeseries writes.

Approach A: Baseline heap + TABLOCK (current default)
Approach B: Heap + no TABLOCK (row-level locking, allows true parallelism)
Approach C: Clustered index + TABLOCK (parallel bulk into B-tree pages)

Each approach is tested with workers ∈ {1, 2, 4, 8} to see how they scale.
"""
from __future__ import annotations

import os, sys, time
import click

sys.path.insert(0, os.path.dirname(__file__))
from bcp_throughput import (
    run_write, estimate_size_bytes, default_connection_string,
    PROFILES,
)
import pygim


# ── Table DDLs ───────────────────────────────────────────────────────────────

DDL_HEAP_TABLOCK = """
DROP TABLE IF EXISTS dbo.ts_exp_heap;
CREATE TABLE dbo.ts_exp_heap (
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL
);"""

DDL_HEAP_NOLOCK = """
DROP TABLE IF EXISTS dbo.ts_exp_heap_nolock;
CREATE TABLE dbo.ts_exp_heap_nolock (
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL
);"""

DDL_CLUSTERED = """
DROP TABLE IF EXISTS dbo.ts_exp_clustered;
CREATE TABLE dbo.ts_exp_clustered (
    id    INT          NOT NULL IDENTITY(1,1) PRIMARY KEY,
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL
);"""

# For clustered table, we need the id column in the data.
# But BCP sends exactly the columns in the Arrow table.
# With IDENTITY(1,1), SQL Server auto-generates the id if we
# SET IDENTITY_INSERT OFF and use KEEPIDENTITY=N.
# Actually, BCP by default skips identity columns if not present in data.
# Let's try without id in the data — BCP should auto-fill identity.
# If that doesn't work, we use an explicit id approach instead.

DDL_CLUSTERED_EXPLICIT = """
DROP TABLE IF EXISTS dbo.ts_exp_clustered;
CREATE TABLE dbo.ts_exp_clustered (
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL,
    CONSTRAINT PK_ts_exp_clustered PRIMARY KEY CLUSTERED (ts, data)
);"""
# Composite PK on (ts, data) — no extra column needed, data matches schema.
# This creates a clustered index that SQL Server can bulk-insert into in parallel.

DDL_INT_PK = """
DROP TABLE IF EXISTS dbo.ts_exp_intpk;
CREATE TABLE dbo.ts_exp_intpk (
    id    INT          NOT NULL PRIMARY KEY,
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL
);"""
# Integer PK like the other profiles — serial id in data, clustered index on id.


def _run_write_with_hint(conn_str, table, df, workers, batch_size, table_hint, packet_size=16384):
    """Write using specific table_hint."""
    from pygim.persistence import acquire_datastore

    store = acquire_datastore(
        conn_str, format="polars",
        batch_size=batch_size,
        bcp_workers=workers,
        table_hint=table_hint,
        packet_size=packet_size,
    )
    payload_bytes = estimate_size_bytes(df)
    nrows = len(df)

    t0 = time.perf_counter()
    metrics = store.save(df, table)
    elapsed = time.perf_counter() - t0

    mb_s = (payload_bytes / 1_048_576) / elapsed if elapsed > 0 else 0.0
    return {
        "rows": nrows,
        "elapsed_s": round(elapsed, 2),
        "mb_s": round(mb_s, 2),
        "rows_s": round(nrows / elapsed) if elapsed > 0 else 0,
        "workers": workers,
        "batch_size": batch_size,
    }


def _ensure_table(conn_str, ddl):
    import pyodbc
    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = True
    cn.execute(ddl)
    cn.close()


def _truncate(conn_str, table):
    import pyodbc
    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = True
    cn.execute(f"TRUNCATE TABLE {table}")
    cn.close()


def _count_rows(conn_str, table):
    import pyodbc
    cn = pyodbc.connect(conn_str, timeout=30)
    row = cn.execute(f"SELECT COUNT(*) FROM {table}").fetchone()
    cn.close()
    return row[0]


@click.command()
@click.option("--conn", default=None)
@click.option("--rows", default=1_000_000, type=int, show_default=True)
@click.option("--iters", default=2, type=int, show_default=True, help="Iterations per config (use median)")
def experiment(conn, rows, iters):
    """Test three parallel write strategies for timeseries data."""
    from tabulate import tabulate
    import statistics

    conn_str = conn or os.getenv("STRESS_CONN", "").strip() or default_connection_string()

    # Generate data — two variants
    schema_no_id = {"ts": "timestamp", "data": "float64"}
    schema_with_id = {"id": "serial", "ts": "timestamp", "data": "float64"}
    click.echo(f"Generating {rows:,} timeseries rows (2 variants) …")
    df = pygim.create_df(schema_no_id, rows=rows, format="arrow")
    df_with_id = pygim.create_df(schema_with_id, rows=rows, format="arrow")
    click.echo(f"  → no-id: {estimate_size_bytes(df) / 1_048_576:.1f} MB")
    click.echo(f"  → with-id: {estimate_size_bytes(df_with_id) / 1_048_576:.1f} MB\n")

    worker_counts = [1, 2, 4, 8]
    batch_size = 500_000  # near-optimal from sweep

    approaches = [
        {
            "name": "A: Heap + TABLOCK",
            "ddl": DDL_HEAP_TABLOCK,
            "table": "dbo.ts_exp_heap",
            "hint": "TABLOCK",
        },
        {
            "name": "B: Heap + no hint",
            "ddl": DDL_HEAP_NOLOCK,
            "table": "dbo.ts_exp_heap_nolock",
            "hint": "",
        },
        {
            "name": "C: Composite PK + TABLOCK",
            "ddl": DDL_CLUSTERED_EXPLICIT,
            "table": "dbo.ts_exp_clustered",
            "hint": "TABLOCK",
        },
        {
            "name": "D: Int PK + TABLOCK",
            "ddl": DDL_INT_PK,
            "table": "dbo.ts_exp_intpk",
            "hint": "TABLOCK",
            "df_key": "with_id",
        },
    ]

    all_results = []

    for approach in approaches:
        click.echo(f"{'='*72}")
        click.echo(f"  {approach['name']}")
        click.echo(f"{'='*72}")
        _ensure_table(conn_str, approach["ddl"])

        for w in worker_counts:
            iter_results = []
            for it in range(iters):
                _truncate(conn_str, approach["table"])
                label = f"  w={w:2d} iter={it+1}/{iters}"
                click.echo(f"{label} … ", nl=False)
                try:
                    use_df = df_with_id if approach.get("df_key") == "with_id" else df
                    r = _run_write_with_hint(
                        conn_str, approach["table"], use_df,
                        workers=w, batch_size=batch_size,
                        table_hint=approach["hint"],
                    )
                    # Verify row count
                    cnt = _count_rows(conn_str, approach["table"])
                    ok = "✓" if cnt == rows else f"✗ ({cnt:,})"
                    click.echo(f"{r['rows_s']:>10,} rows/s  {r['mb_s']:6.2f} MB/s  {r['elapsed_s']:.2f}s  [{ok}]")
                    iter_results.append(r)
                except Exception as e:
                    click.echo(f"FAILED: {e}")

            if iter_results:
                median_rows_s = statistics.median([r["rows_s"] for r in iter_results])
                median_mb_s = statistics.median([r["mb_s"] for r in iter_results])
                median_elapsed = statistics.median([r["elapsed_s"] for r in iter_results])
                all_results.append({
                    "Approach": approach["name"],
                    "Workers": w,
                    "rows/s": f"{median_rows_s:,.0f}",
                    "MB/s": f"{median_mb_s:.2f}",
                    "Elapsed": f"{median_elapsed:.2f}s",
                    "_rows_s": median_rows_s,
                })
        click.echo()

    # Summary table
    click.echo(f"\n{'='*72}")
    click.echo("SUMMARY (median of iterations)")
    click.echo(f"{'='*72}")
    click.echo(tabulate(all_results, headers="keys", tablefmt="pipe"))

    # Find best overall
    if all_results:
        best = max(all_results, key=lambda r: r["_rows_s"])
        click.echo(f"\n★ BEST: {best['Approach']}, workers={best['Workers']} → {best['rows/s']} rows/s ({best['MB/s']} MB/s)")

        # Show scaling factor for each approach
        click.echo(f"\nScaling analysis (vs workers=1 within same approach):")
        for approach in approaches:
            rows_at = {}
            for r in all_results:
                if r["Approach"] == approach["name"]:
                    rows_at[r["Workers"]] = r["_rows_s"]
            if 1 in rows_at:
                base = rows_at[1]
                for w in sorted(rows_at):
                    ratio = rows_at[w] / base
                    click.echo(f"  {approach['name']:30s} w={w:2d}: {rows_at[w]:>10,.0f} rows/s  ({ratio:.2f}x vs w=1)")


if __name__ == "__main__":
    experiment()
