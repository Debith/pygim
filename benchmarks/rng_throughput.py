#!/usr/bin/env python3
"""Throughput benchmark: pygim.rng vs NumPy bit generators.

Measures bulk float64 (and optionally uint64) generation into preallocated
buffers, reporting GB/s (8 bytes/element) as best-of-N to suppress noise.

Contenders
----------
- numpy PCG64      : default_rng() default
- numpy SFC64      : numpy's fastest bit generator
- numpy Philox     : counter-based reference point
- pygim 1-thread   : AVX2 xoshiro256++ x16 streams, single core
- pygim scalar     : same engine, SIMD disabled (fallback path)
- pygim auto       : block-parallel threads (memory-bandwidth bound)

Usage
-----
    python benchmarks/rng_throughput.py
    python benchmarks/rng_throughput.py --sizes 1e6,1e8 --reps 7 --uint64
"""

from __future__ import annotations

import os
import time

import click
import numpy as np

try:
    from pygim.rng import Rng
except ImportError as exc:  # pragma: no cover
    raise SystemExit(f"pygim.rng extension not built: {exc}")


def best_of(fn, reps: int) -> float:
    best = float("inf")
    for _ in range(reps):
        t = time.perf_counter()
        fn()
        best = min(best, time.perf_counter() - t)
    return best


def gbps(n: int, seconds: float) -> float:
    return n * 8 / seconds / 1e9


@click.command()
@click.option("--sizes", default="1e6,1e7,1e8", help="Comma-separated element counts.")
@click.option("--reps", default=5, show_default=True, help="Repetitions; best is reported.")
@click.option("--uint64", "do_uint64", is_flag=True, help="Also benchmark raw uint64 output.")
def main(sizes: str, reps: int, do_uint64: bool) -> None:
    print(f"PID: {os.getpid()}")
    ns = [int(float(s)) for s in sizes.split(",")]
    print(f"numpy {np.__version__}; pygim.rng simd={Rng(0).simd}; sizes={ns}; best of {reps}\n")

    numpy_rows: list[tuple[str, list[float]]] = []
    for label, bitgen in [
        ("numpy PCG64 (default)", np.random.PCG64),
        ("numpy SFC64", np.random.SFC64),
        ("numpy Philox", np.random.Philox),
    ]:
        speeds = []
        for n in ns:
            gen = np.random.Generator(bitgen(42))
            out = np.empty(n)
            speeds.append(gbps(n, best_of(lambda: gen.random(out=out), reps)))
        numpy_rows.append((label, speeds))

    pygim_rows: list[tuple[str, list[float]]] = []
    for label, kwargs in [
        ("pygim.rng 1 thread", {"threads": 1}),
        ("pygim.rng scalar 1T", {"threads": 1, "simd": False}),
        ("pygim.rng auto threads", {"threads": 0}),
    ]:
        speeds = []
        for n in ns:
            rng = Rng(42, **kwargs)
            out = np.empty(n)
            speeds.append(gbps(n, best_of(lambda: rng.fill(out), reps)))
        pygim_rows.append((label, speeds))

    header = f"{'float64 fill':<26}" + "".join(f"{f'n={n:,}':>16}" for n in ns)
    print(header)
    print("-" * len(header))
    for label, speeds in numpy_rows + pygim_rows:
        print(f"{label:<26}" + "".join(f"{s:>10.2f} GB/s".rjust(16) for s in speeds))

    best_numpy = [max(r[1][i] for r in numpy_rows) for i in range(len(ns))]
    best_pygim = [max(r[1][i] for r in pygim_rows) for i in range(len(ns))]
    print("-" * len(header))
    print(f"{'speedup vs best numpy':<26}"
          + "".join(f"{best_pygim[i] / best_numpy[i]:>15.2f}x" for i in range(len(ns))))

    if do_uint64:
        print(f"\n{'uint64 fill':<26}" + "".join(f"{f'n={n:,}':>16}" for n in ns))
        for label, kwargs in [("pygim.rng 1 thread", {"threads": 1}),
                              ("pygim.rng auto threads", {"threads": 0})]:
            speeds = []
            for n in ns:
                rng = Rng(42, **kwargs)
                out = np.empty(n, dtype=np.uint64)
                speeds.append(gbps(n, best_of(lambda: rng.fill_uint64(out), reps)))
            print(f"{label:<26}" + "".join(f"{s:>10.2f} GB/s".rjust(16) for s in speeds))
        # NOTE: asymmetric baseline — integers() allocates a fresh array per
        # call and pays bounded-range handling; numpy exposes no public
        # preallocated raw-uint64 fill to compare against.
        speeds = []
        for n in ns:
            gen = np.random.Generator(np.random.SFC64(42))
            speeds.append(gbps(n, best_of(lambda: gen.integers(0, 2**64, size=n, dtype=np.uint64), reps)))
        print(f"{'numpy SFC64 integers':<26}" + "".join(f"{s:>10.2f} GB/s".rjust(16) for s in speeds))


if __name__ == "__main__":
    main()
