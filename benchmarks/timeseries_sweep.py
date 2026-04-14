#!/usr/bin/env python3
"""Timeseries parameter sweep — finds optimal BCP settings for 2-column data."""
from __future__ import annotations

import os, sys, time
import click

# Add benchmarks dir to path so we can import from bcp_throughput
sys.path.insert(0, os.path.dirname(__file__))
from bcp_throughput import (
    run_write, run_load, ensure_table, truncate_table,
    default_connection_string, PROFILES, estimate_size_bytes,
)
import pygim

def _require(mod):
    import importlib
    try:
        return importlib.import_module(mod)
    except ModuleNotFoundError:
        click.echo(f"ERROR: '{mod}' required", err=True)
        sys.exit(1)


@click.command()
@click.option("--conn", default=None)
@click.option("--rows", default=1_000_000, type=int, show_default=True)
def sweep(conn, rows):
    """Run ~56 experiments to find optimal timeseries BCP settings."""
    from tabulate import tabulate

    conn_str = conn or os.getenv("STRESS_CONN", "").strip() or default_connection_string()
    pyodbc = _require("pyodbc")

    profile = PROFILES["timeseries"]
    table = profile["table"]

    # Ensure table exists
    ensure_table(conn_str, profile["ddl"], pyodbc)

    # Generate data once
    click.echo(f"Generating {rows:,} timeseries rows …")
    df = pygim.create_df(profile["schema"], rows=rows, format="arrow")
    click.echo(f"  → {estimate_size_bytes(df) / 1_048_576:.1f} MB")

    # ── Write sweep ──────────────────────────────────────────────
    write_grid = []
    for w in [1, 2, 4, 8, 16]:
        for bs in [1000, 10000, 50000, 100000, 500000, 1000000]:
            write_grid.append({"workers": w, "batch_size": bs,
                             "packet_size": 16384, "block_size": 8192})

    click.echo(f"\n{'='*72}")
    click.echo(f"WRITE SWEEP: {len(write_grid)} experiments")
    click.echo(f"{'='*72}")

    write_results = []
    for i, params in enumerate(write_grid, 1):
        truncate_table(conn_str, table, pyodbc)
        label = f"[{i:2d}/{len(write_grid)}] w={params['workers']:2d} bs={params['batch_size']:>7,} ps={params['packet_size']}"
        click.echo(f"  {label} … ", nl=False)
        try:
            r = run_write(conn_str, table, df, "timeseries",
                         bcp_workers=params["workers"],
                         batch_size=params["batch_size"],
                         block_size=params["block_size"],
                         packet_size=params["packet_size"])
            click.echo(f"{r['rows_s']:>10,.0f} rows/s  {r['mb_s']:6.2f} MB/s  {r['elapsed_s']:.2f}s")
            write_results.append({**params, **r})
        except Exception as e:
            click.echo(f"FAILED: {e}")

    # ── Load sweep ───────────────────────────────────────────────
    # Make sure table has data for loading
    truncate_table(conn_str, table, pyodbc)
    run_write(conn_str, table, df, "timeseries", bcp_workers=8,
              batch_size=100000, block_size=8192, packet_size=16384)

    load_grid = []
    for w in [1, 2, 4, 8]:
        for blk in [1024, 4096, 8192, 16384, 32768]:
            load_grid.append({"workers": w, "block_size": blk, "packet_size": 16384})

    click.echo(f"\n{'='*72}")
    click.echo(f"LOAD SWEEP: {len(load_grid)} experiments")
    click.echo(f"{'='*72}")

    load_results = []
    for i, params in enumerate(load_grid, 1):
        label = f"[{i:2d}/{len(load_grid)}] w={params['workers']:2d} blk={params['block_size']:>5}"
        click.echo(f"  {label} … ", nl=False)
        try:
            r = run_load(conn_str, table, "timeseries",
                        load_workers=params["workers"],
                        block_size=params["block_size"],
                        packet_size=params["packet_size"])
            click.echo(f"{r['rows_s']:>10,.0f} rows/s  {r['mb_s']:6.2f} MB/s  {r['elapsed_s']:.2f}s")
            load_results.append({**params, **r})
        except Exception as e:
            click.echo(f"FAILED: {e}")

    # ── Packet size sweep (best write config) ────────────────────
    pkt_results = []
    if write_results:
        best_write = max(write_results, key=lambda x: x["rows_s"])
        best_w = best_write["workers"]
        best_bs = best_write["batch_size"]

        click.echo(f"\n{'='*72}")
        click.echo(f"PACKET SIZE SWEEP (w={best_w}, bs={best_bs:,}): 6 experiments")
        click.echo(f"{'='*72}")

        for ps in [512, 1024, 4096, 8192, 16384, 32768]:
            truncate_table(conn_str, table, pyodbc)
            click.echo(f"  packet_size={ps:>5} … ", nl=False)
            try:
                r = run_write(conn_str, table, df, "timeseries",
                             bcp_workers=best_w, batch_size=best_bs,
                             block_size=8192, packet_size=ps)
                click.echo(f"{r['rows_s']:>10,.0f} rows/s  {r['mb_s']:6.2f} MB/s  {r['elapsed_s']:.2f}s")
                pkt_results.append({"packet_size": ps, **r})
            except Exception as e:
                click.echo(f"FAILED: {e}")

    # ── Summary tables ───────────────────────────────────────────
    click.echo(f"\n{'='*72}")
    click.echo("WRITE RESULTS (sorted by rows/s)")
    click.echo(f"{'='*72}")

    write_results.sort(key=lambda x: x["rows_s"], reverse=True)
    write_table = []
    for r in write_results:
        bcp = r.get("bcp_metrics", {})
        write_table.append([
            r["workers"], f"{r['batch_size']:,}",
            f"{r['rows_s']:,.0f}", f"{r['mb_s']:.2f}",
            f"{r['elapsed_s']:.2f}",
            f"{bcp.get('row_loop_seconds', 0):.2f}",
            f"{bcp.get('batch_flush_seconds', 0):.2f}",
        ])
    click.echo(tabulate(write_table,
                        headers=["Workers", "BatchSize", "rows/s", "MB/s",
                                "Elapsed", "RowLoop", "Flush"],
                        tablefmt="github"))

    click.echo(f"\n{'='*72}")
    click.echo("LOAD RESULTS (sorted by rows/s)")
    click.echo(f"{'='*72}")

    load_results.sort(key=lambda x: x["rows_s"], reverse=True)
    load_table = []
    for r in load_results:
        load_table.append([
            r["workers"], r["block_size"],
            f"{r['rows_s']:,.0f}", f"{r['mb_s']:.2f}",
            f"{r['elapsed_s']:.2f}",
        ])
    click.echo(tabulate(load_table,
                        headers=["Workers", "BlockSize", "rows/s", "MB/s", "Elapsed"],
                        tablefmt="github"))

    if pkt_results:
        click.echo(f"\n{'='*72}")
        click.echo("PACKET SIZE RESULTS (sorted by rows/s)")
        click.echo(f"{'='*72}")
        pkt_results.sort(key=lambda x: x["rows_s"], reverse=True)
        pkt_table = []
        for r in pkt_results:
            pkt_table.append([
                r["packet_size"],
                f"{r['rows_s']:,.0f}", f"{r['mb_s']:.2f}",
                f"{r['elapsed_s']:.2f}",
            ])
        click.echo(tabulate(pkt_table,
                            headers=["PacketSize", "rows/s", "MB/s", "Elapsed"],
                            tablefmt="github"))

    # ── Top 5 ────────────────────────────────────────────────────
    click.echo(f"\n{'='*72}")
    click.echo("TOP 5 WRITE CONFIGS")
    click.echo(f"{'='*72}")
    for i, r in enumerate(write_results[:5], 1):
        click.echo(f"  #{i}: workers={r['workers']} batch_size={r['batch_size']:,} "
                   f"→ {r['rows_s']:,.0f} rows/s ({r['mb_s']:.2f} MB/s)")

    click.echo(f"\nTOP 5 LOAD CONFIGS")
    click.echo(f"{'='*72}")
    for i, r in enumerate(load_results[:5], 1):
        click.echo(f"  #{i}: workers={r['workers']} block_size={r['block_size']:,} "
                   f"→ {r['rows_s']:,.0f} rows/s ({r['mb_s']:.2f} MB/s)")


if __name__ == "__main__":
    sweep()
