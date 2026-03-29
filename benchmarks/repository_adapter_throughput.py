#!/usr/bin/env python3
"""
Repository adapter throughput benchmark.

Measures the overhead of the adapter layer (RepositoryAdapter → Repository core)
independent of actual DB I/O. Uses placeholder backend.

Usage:
    python benchmarks/repository_adapter_throughput.py [--iterations N] [--warmup W]
"""
from __future__ import annotations

import time

import click

from pygim._repository_test import Query, Repository


def _noop():
    pass


def _bench(fn, iterations: int) -> float:
    """Run fn for iterations, return total elapsed nanoseconds."""
    start = time.perf_counter_ns()
    for _ in range(iterations):
        fn()
    return time.perf_counter_ns() - start


def _run_scenario(
    label: str,
    fn,
    iterations: int,
    warmup: int,
    n_transforms: int,
) -> dict:
    # warmup
    for _ in range(warmup):
        fn()

    elapsed_ns = _bench(fn, iterations)
    total_ms = elapsed_ns / 1_000_000
    per_call_us = elapsed_ns / iterations / 1_000
    calls_per_sec = iterations / (elapsed_ns / 1_000_000_000) if elapsed_ns > 0 else float("inf")

    return {
        "operation": label,
        "transforms": n_transforms,
        "iterations": iterations,
        "total_ms": total_ms,
        "per_call_us": per_call_us,
        "calls_per_sec": calls_per_sec,
    }


def _print_results(results: list[dict]) -> None:
    header = f"{'Operation':<35} {'Xforms':>6} {'Iters':>8} {'Total(ms)':>10} {'Per-call(µs)':>13} {'Calls/sec':>12}"
    sep = "-" * len(header)
    click.echo(sep)
    click.echo(header)
    click.echo(sep)
    for r in results:
        click.echo(
            f"{r['operation']:<35} {r['transforms']:>6} {r['iterations']:>8} "
            f"{r['total_ms']:>10.2f} {r['per_call_us']:>13.3f} {r['calls_per_sec']:>12,.0f}"
        )
    click.echo(sep)


@click.command()
@click.option("--iterations", "-n", default=10_000, show_default=True, help="Number of measured iterations.")
@click.option("--warmup", "-w", default=100, show_default=True, help="Warmup iterations before measurement.")
def main(iterations: int, warmup: int) -> None:
    """Benchmark repository adapter overhead (placeholder backend)."""
    click.echo(f"Repository adapter throughput benchmark")
    click.echo(f"  iterations={iterations:,}  warmup={warmup:,}")
    click.echo()

    query = Query().select("id").select("name").from_table("bench_table").where("id > 0").limit(100)
    results: list[dict] = []

    # ── No transforms ────────────────────────────────────────────────────
    repo = Repository("bench_conn", format="polars", pool_size=2)

    results.append(_run_scenario(
        "save (no transforms)", lambda: repo.save("bench_table", bcp_workers=1),
        iterations, warmup, 0,
    ))
    results.append(_run_scenario(
        "load string (no transforms)", lambda: repo.load("bench_table", load_workers=1),
        iterations, warmup, 0,
    ))
    results.append(_run_scenario(
        "load Query (no transforms)", lambda: repo.load(query, load_workers=1),
        iterations, warmup, 0,
    ))

    # ── With transforms ──────────────────────────────────────────────────
    for n in (1, 5, 10):
        repo_t = Repository("bench_conn", format="polars", pool_size=2)
        for _ in range(n):
            repo_t.add_pre_transform(_noop)
            repo_t.add_post_transform(_noop)

        results.append(_run_scenario(
            f"save ({n} pre + {n} post transforms)",
            lambda r=repo_t: r.save("bench_table", bcp_workers=1),
            iterations, warmup, n * 2,
        ))

    _print_results(results)


if __name__ == "__main__":
    main()
