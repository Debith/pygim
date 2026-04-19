#!/usr/bin/env python3
"""Experiment: alternative bulk insert methods for narrow timeseries data.

Compares:
  1. BCP row-by-row (current, workers=1, best config from sweep)
  2. Parameterized INSERT with row_array_size (ODBC array binding)
  3. BCP with larger packet sizes on non-SSL connections

The key hypothesis: for narrow schemas, ODBC array-bound INSERT may beat
BCP because it can send N rows per network round-trip vs BCP's 1 sendrow per row.
"""
from __future__ import annotations

import os, sys, time
import click
import statistics

sys.path.insert(0, os.path.dirname(__file__))
from bcp_throughput import (
    run_write, estimate_size_bytes, default_connection_string,
)
import pygim


DDL = """
DROP TABLE IF EXISTS dbo.ts_exp_insert;
CREATE TABLE dbo.ts_exp_insert (
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL
);"""

DDL_BCP = """
DROP TABLE IF EXISTS dbo.ts_exp_bcp;
CREATE TABLE dbo.ts_exp_bcp (
    ts    DATETIME2(6) NOT NULL,
    data  FLOAT        NOT NULL
);"""


def _exec(conn_str, sql):
    import pyodbc
    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = True
    cn.execute(sql)
    cn.close()


def _count(conn_str, table):
    import pyodbc
    cn = pyodbc.connect(conn_str, timeout=30)
    row = cn.execute(f"SELECT COUNT(*) FROM {table}").fetchone()
    cn.close()
    return row[0]


def insert_via_executemany(conn_str, table, df_arrow, batch_size):
    """Use pyodbc executemany with fast_executemany=True (array binding)."""
    import pyodbc
    import pyarrow as pa

    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = False
    cursor = cn.cursor()
    cursor.fast_executemany = True  # enables ODBC array parameter binding

    sql = f"INSERT INTO {table} (ts, data) VALUES (?, ?)"

    # Convert Arrow to list of tuples in batches
    nrows = len(df_arrow)
    total_sent = 0
    t0 = time.perf_counter()

    for offset in range(0, nrows, batch_size):
        end = min(offset + batch_size, nrows)
        chunk = df_arrow.slice(offset, end - offset)
        # Convert to Python tuples
        ts_col = chunk.column("ts")
        data_col = chunk.column("data")
        rows = [(ts_col[i].as_py(), data_col[i].as_py()) for i in range(len(chunk))]
        cursor.executemany(sql, rows)
        total_sent += len(rows)

    cn.commit()
    elapsed = time.perf_counter() - t0
    cursor.close()
    cn.close()

    return {
        "rows": total_sent,
        "elapsed_s": round(elapsed, 2),
        "rows_s": round(total_sent / elapsed) if elapsed > 0 else 0,
    }


def insert_via_executemany_numpy(conn_str, table, df_arrow, batch_size):
    """Use pyodbc executemany with numpy arrays for faster conversion."""
    import pyodbc
    import numpy as np
    from datetime import datetime

    cn = pyodbc.connect(conn_str, timeout=30)
    cn.autocommit = False
    cursor = cn.cursor()
    cursor.fast_executemany = True

    sql = f"INSERT INTO {table} (ts, data) VALUES (?, ?)"

    # Pre-convert entire columns
    ts_np = df_arrow.column("ts").to_numpy()
    data_np = df_arrow.column("data").to_numpy()

    nrows = len(df_arrow)
    total_sent = 0
    t0 = time.perf_counter()

    for offset in range(0, nrows, batch_size):
        end = min(offset + batch_size, nrows)
        ts_batch = ts_np[offset:end]
        data_batch = data_np[offset:end]
        # Convert timestamps to Python datetime for pyodbc
        rows = [(ts_batch[i].astype('datetime64[us]').astype(datetime), float(data_batch[i]))
                for i in range(len(ts_batch))]
        cursor.executemany(sql, rows)
        total_sent += len(rows)

    cn.commit()
    elapsed = time.perf_counter() - t0
    cursor.close()
    cn.close()

    return {
        "rows": total_sent,
        "elapsed_s": round(elapsed, 2),
        "rows_s": round(total_sent / elapsed) if elapsed > 0 else 0,
    }


@click.command()
@click.option("--conn", default=None)
@click.option("--rows", default=500_000, type=int, show_default=True)
@click.option("--iters", default=3, type=int, show_default=True)
def experiment(conn, rows, iters):
    """Compare BCP vs parameterized INSERT for narrow timeseries."""
    from tabulate import tabulate

    conn_str = conn or os.getenv("STRESS_CONN", "").strip() or default_connection_string()

    schema = {"ts": "timestamp", "data": "float64"}
    click.echo(f"Generating {rows:,} timeseries rows …")
    df = pygim.create_df(schema, rows=rows, format="arrow")
    payload_mb = estimate_size_bytes(df) / 1_048_576
    click.echo(f"  → {payload_mb:.1f} MB\n")

    configs = [
        {"name": "BCP w=1 bs=500K", "method": "bcp", "workers": 1, "batch_size": 500_000},
        {"name": "BCP w=1 bs=1M", "method": "bcp", "workers": 1, "batch_size": 1_000_000},
        {"name": "INSERT fast_exec bs=1K", "method": "insert", "batch_size": 1_000},
        {"name": "INSERT fast_exec bs=10K", "method": "insert", "batch_size": 10_000},
        {"name": "INSERT fast_exec bs=50K", "method": "insert", "batch_size": 50_000},
        {"name": "INSERT fast_exec bs=100K", "method": "insert", "batch_size": 100_000},
    ]

    all_results = []

    for cfg in configs:
        click.echo(f"{'─'*60}")
        click.echo(f"  {cfg['name']}")
        click.echo(f"{'─'*60}")

        iter_results = []
        for it in range(iters):
            click.echo(f"  iter {it+1}/{iters} … ", nl=False)
            try:
                if cfg["method"] == "bcp":
                    _exec(conn_str, DDL_BCP)
                    r = run_write(conn_str, "dbo.ts_exp_bcp", df, "timeseries",
                                 bcp_workers=cfg["workers"],
                                 batch_size=cfg["batch_size"])
                    cnt = _count(conn_str, "dbo.ts_exp_bcp")
                    result = {"rows": r["rows"], "elapsed_s": round(r["elapsed_s"], 2),
                              "rows_s": round(r["rows_s"])}
                else:
                    _exec(conn_str, DDL)
                    result = insert_via_executemany(conn_str, "dbo.ts_exp_insert", df, cfg["batch_size"])
                    cnt = _count(conn_str, "dbo.ts_exp_insert")

                ok = "✓" if cnt == rows else f"✗ ({cnt:,})"
                click.echo(f"{result['rows_s']:>10,} rows/s  {result['elapsed_s']:.2f}s  [{ok}]")
                iter_results.append(result)
            except Exception as e:
                click.echo(f"FAILED: {e}")

        if iter_results:
            med_rows_s = statistics.median([r["rows_s"] for r in iter_results])
            med_elapsed = statistics.median([r["elapsed_s"] for r in iter_results])
            med_mb_s = round(payload_mb / med_elapsed, 2) if med_elapsed > 0 else 0
            all_results.append({
                "Config": cfg["name"],
                "rows/s": f"{med_rows_s:,.0f}",
                "MB/s": f"{med_mb_s:.2f}",
                "Elapsed": f"{med_elapsed:.2f}s",
                "_rows_s": med_rows_s,
            })
        click.echo()

    click.echo(f"\n{'='*60}")
    click.echo("SUMMARY (median of iterations)")
    click.echo(f"{'='*60}")
    click.echo(tabulate(all_results, headers="keys", tablefmt="pipe"))

    if all_results:
        best = max(all_results, key=lambda r: r["_rows_s"])
        click.echo(f"\n★ BEST: {best['Config']} → {best['rows/s']} rows/s ({best['MB/s']} MB/s)")


if __name__ == "__main__":
    experiment()
