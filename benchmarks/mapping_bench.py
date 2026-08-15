"""mapping toolkit benchmarks: gimdict family vs dict.

Honest framing (docs/design/mapping_toolkit.md): per-op access through the
binding is expected to LOSE to dict — the published numbers are crossovers
and costs, not blanket claims. Sections:

1. construct  — build from n items
2. lookup     — n hits on an n-key map
3. iterate    — full key walk
4. merge      — a | b fold (gimdict) vs {**a, **b} (dict)

Run:  python benchmarks/mapping_bench.py [--no-save]
"""

import time
from pathlib import Path  # noqa: F401  (parity with sibling benchmarks)

from tabulate import tabulate

from _results import save, wants_save
from pygim import utils

REPS = 5
SIZES = [8, 64, 1024, 16384]


def best(fn):
    times = []
    for _ in range(REPS):
        t0 = time.perf_counter()
        fn()
        times.append(time.perf_counter() - t0)
    return min(times)


def data(n):
    return {f"key{i:05d}": i for i in range(n)}


def bench():
    rows = []
    for n in SIZES:
        src = data(n)
        keys = list(src)
        gd = utils.gimdict(src)
        fz = utils.gimdict(src, frozen=True)
        other = {k: 1 for k in keys[: n // 2]} | {f"new{i}": i for i in range(n // 2)}
        gother = utils.gimdict(other)

        row = {
            "n": n,
            "construct_dict_s": best(lambda: dict(src)),
            "construct_gimdict_s": best(lambda: utils.gimdict(src)),
            "construct_frozen_s": best(lambda: utils.gimdict(src, frozen=True)),
            "lookup_dict_s": best(lambda: [src[k] for k in keys]),
            "lookup_gimdict_s": best(lambda: [gd[k] for k in keys]),
            "lookup_frozen_s": best(lambda: [fz[k] for k in keys]),
            "iterate_dict_s": best(lambda: list(src)),
            "iterate_gimdict_s": best(lambda: list(gd)),
            "merge_dict_s": best(lambda: {**src, **other}),
            "merge_gimdict_s": best(lambda: gd | gother),
        }
        rows.append(row)
    return rows


def show(rows):
    def fmt(row, op):
        ours = row[f"{op}_gimdict_s"]
        ref = row[f"{op}_dict_s"]
        return f"{ours * 1e6:9.1f} ({ref / ours:4.2f}x)"

    table = [[r["n"],
              fmt(r, "construct"), fmt(r, "lookup"), fmt(r, "iterate"), fmt(r, "merge"),
              f"{r['lookup_frozen_s'] * 1e6:9.1f}"]
             for r in rows]
    print("\n== gimdict vs dict, µs (ratio: >1x means gimdict faster) ==")
    print(tabulate(table,
                   headers=["n", "construct", "lookup", "iterate", "merge", "frozen lookup µs"],
                   tablefmt="github"))


if __name__ == "__main__":
    rows = bench()
    show(rows)
    if wants_save():
        print(f"\nRun recorded -> {save('mapping_bench', {'ops': rows}, reps=REPS)}")
