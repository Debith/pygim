"""pygim.path as a handle: construction into a fresh store (rows created) and into a
store that already holds the rows, derived paths by row, and memory per object.

Sections:

1. **construct** — path(s) over N distinct strings into a fresh store (rows
   created) and into a store already holding them (lookups only), vs
   pathlib.PurePath(s).
2. **derived** — p.parent and p / "x", by row.
3. **memory** — the table's bytes per path, and RSS per handle.

Run:  python benchmarks/path_store.py [--no-save]
Each run appends its raw measurements + environment metadata to
``results/path_store.jsonl`` (see ``_results.py``).
"""

import gc
import os
import pathlib

from tabulate import tabulate

import pygim
from pygim import pathlike
from _results import save, wants_save

from _bench import REPS, best, best_fresh, corpus

N = 200_000
path = pygim.path


def ns(seconds, n=N):
    return seconds / n * 1e9


def bench_construct(strs):
    rows, raw = [], {}

    def rec(label, fn, timer=best):
        t, r = timer(fn)
        raw[label] = {"seconds": t}
        rows.append([label, f"{ns(t):,.0f}"])
        return r

    rec("pathlib.PurePath(s)", lambda: [pathlib.PurePath(s) for s in strs])

    def fresh():
        st = pathlike.PathStore()
        return [path(s, store=st) for s in strs]
    rec("path(s, store=fresh)  (rows created)", fresh, timer=best_fresh)
    st = pathlike.PathStore()
    held = [path(s, store=st) for s in strs]
    rec("path(s, store=st)  (rows present: lookups + a handle)", lambda: [path(s, store=st) for s in strs])
    del held
    print(f"\n## 1. construct, {N:,} distinct paths (ns per path, best of {REPS})\n")
    print(tabulate(rows, headers=["construct", "ns/path"], tablefmt="github"))
    return raw


def bench_derived(strs):
    rows, raw = [], {}

    def rec(label, fn, timer=best):
        t, _ = timer(fn)
        raw[label] = {"seconds": t}
        rows.append([label, f"{ns(t):,.0f}"])

    st = pathlike.PathStore()
    objs = [path(s, store=st) for s in strs]
    pl_objs = [pathlib.PurePath(s) for s in strs]
    rec("PurePath.parent", lambda: [p.parent for p in pl_objs])
    rec("p.parent  (by row)", lambda: [p.parent for p in objs])
    rec("PurePath / 'x'", lambda: [p / "x" for p in pl_objs])
    rec("p / 'x'   (one component: child row)", lambda: [p / "x" for p in objs])
    rec("p.with_suffix('.j')  (value route)", lambda: [p.with_suffix(".j") for p in objs])
    rec("p.name  (a view)", lambda: [p.name for p in objs])
    print(f"\n## 2. derived paths, {N:,} paths (ns per operation, best of {REPS})\n")
    print(tabulate(rows, headers=["operation", "ns/op"], tablefmt="github"))
    return raw


_MEM_PROBE = """
import gc, os, sys
sys.path.insert(0, {bench_dir!r})
from _bench import corpus, rss_mb
import pygim
from pygim import pathlike
strs = corpus({n})
gc.collect()
m0 = rss_mb()
if {variant!r} == "pathlib":
    objs = [__import__("pathlib").PurePath(s) for s in strs]
    print((rss_mb() - m0) * 2**20 / {n})
else:
    st = pathlike.PathStore()
    objs = [pygim.path(s, store=st) for s in strs]
    total = (rss_mb() - m0) * 2**20 / {n}
    store = st.stats()["bytes"] / {n}
    del objs
    gc.collect()
    print(total, store, 0)
"""


def bench_memory():
    """Each variant in its own process: an RSS delta only means something on a fresh heap."""
    import subprocess
    import sys

    bench_dir = str(pathlib.Path(__file__).resolve().parent)

    def probe(variant):
        out = subprocess.run([sys.executable, "-c", _MEM_PROBE.format(bench_dir=bench_dir, n=N, variant=variant)],
                             capture_output=True, text=True, check=True).stdout.split()
        return [float(x) for x in out]

    (pl_per,) = probe("pathlib")
    total, store, _ = probe("store")
    rows = [["rss per pathlib.PurePath object", f"{pl_per:.0f}"],
            ["rss per path() handle, table included", f"{total:.0f}"],
            ["  of which the table, per path", f"{store:.0f}"],
            ["  of which the handle", f"{total - store:.0f}"]]
    print(f"\n## 3. memory, {N:,} paths (each variant in a fresh process)\n")
    print(tabulate(rows, headers=["measure", "bytes / count"], tablefmt="github"))
    return {"rss_per_purepath": pl_per, "rss_per_handle_total": total, "table_bytes_per_path": store}


def main():
    strs = corpus(N)
    print(f"engines {[e.name for e in pathlike.ENGINES]}")
    sections = {"construct": bench_construct(strs), "derived": bench_derived(strs), "memory": bench_memory()}
    if wants_save():
        print(f"\nRun recorded -> {save('path_store', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
