"""PathStore flyweight benchmarks: pygim.path() through the store vs the raw file()
constructor, cold (every value new) and warm (the value's object is alive).

Sections:

1. **construct** — path(s) over N distinct strings, cold (fresh store) and warm
   (a second pass while the first pass's objects are held), vs file(s) and
   pathlib.PurePath(s).
2. **derived** — p.parent and p / "x" cold and warm.
3. **memory** — the store's bytes per interned path with the objects dead, and
   RSS per live object.

Run:  python benchmarks/path_store.py [--no-save]
Each run appends its raw measurements + environment metadata to
``results/path_store.jsonl`` (see ``_results.py``).
"""

import gc
import os
import pathlib
import time

from tabulate import tabulate

import pygim
from pygim import pathlike
from _results import save, wants_save
from pathset_prototype import corpus

REPS = 3
N = 200_000
path = pygim.path


def best(fn, reps=REPS):
    t_best, r = float("inf"), None
    for _ in range(reps):
        gc.collect()
        gc.disable()
        t0 = time.perf_counter()
        r = fn()
        t_best = min(t_best, time.perf_counter() - t0)
        gc.enable()
    return t_best, r


def best_fresh(fn, reps=REPS):
    """best() but the previous pass's objects are dropped before each timing —
    so an interned-but-dead value is measured as such, not as warm."""
    t_best, r = float("inf"), None
    for _ in range(reps):
        r = None
        gc.collect()
        gc.disable()
        t0 = time.perf_counter()
        r = fn()
        t_best = min(t_best, time.perf_counter() - t0)
        gc.enable()
    return t_best, r


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
    rec("file(s)  (raw constructor, no store)", lambda: [pathlike.file(s) for s in strs])

    def cold():
        with pathlike.use_store(pathlike.PathStore()):
            return [path(s) for s in strs]
    rec("path(s) cold  (fresh store: intern + grow + new objects)", cold, timer=best_fresh)

    st = pathlike.PathStore()
    with pathlike.use_store(st):
        held = [path(s) for s in strs]
        rec("path(s) warm  (objects alive: lookup only)", lambda: [path(s) for s in strs])
        del held
        rec("path(s) cool  (rows interned, objects dead: lookup + new objects)",
            lambda: [path(s) for s in strs], timer=best_fresh)
    print(f"\n## 1. construct, {N:,} distinct paths (ns per path, best of {REPS})\n")
    print(tabulate(rows, headers=["construct", "ns/path"], tablefmt="github"))
    return raw


def bench_derived(strs):
    rows, raw = [], {}

    def rec(label, fn, timer=best):
        t, _ = timer(fn)
        raw[label] = {"seconds": t}
        rows.append([label, f"{ns(t):,.0f}"])

    raw_files = [pathlike.file(s) for s in strs]
    rec("f.parent  (raw files, no store)", lambda: [f.parent for f in raw_files])
    st = pathlike.PathStore()
    with pathlike.use_store(st):
        objs = [path(s) for s in strs]
        rec("p.parent  cool (parents interned, not alive)", lambda: [p.parent for p in objs], timer=best_fresh)
        parents = [p.parent for p in objs]
        rec("p.parent  warm (parents alive)", lambda: [p.parent for p in objs])
        rec("p / 'x'   cool", lambda: [p / "x" for p in objs], timer=best_fresh)
        kids = [p / "x" for p in objs]
        rec("p / 'x'   warm", lambda: [p / "x" for p in objs])
        del parents, kids
    print(f"\n## 2. derived paths, {N:,} paths (ns per operation, best of {REPS})\n")
    print(tabulate(rows, headers=["operation", "ns/op"], tablefmt="github"))
    return raw


_MEM_PROBE = """
import gc, os, sys
sys.path.insert(0, {bench_dir!r})
from pathset_prototype import corpus, rss_mb
import pygim
from pygim import pathlike
strs = corpus({n})
gc.collect()
m0 = rss_mb()
if {variant!r} == "raw":
    objs = [pathlike.file(s) for s in strs]
    print((rss_mb() - m0) * 2**20 / {n})
else:
    st = pathlike.PathStore()
    with pathlike.use_store(st):
        objs = [pygim.path(s) for s in strs]
        total = (rss_mb() - m0) * 2**20 / {n}
        s = st.stats()
        store = (s["table_bytes"] + s["slot_bytes"]) / {n}
        del objs
        gc.collect()
        print(total, store, st.stats()["live"])
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

    (raw_per,) = probe("raw")
    total, store, live = probe("store")
    rows = [["rss per raw file() object", f"{raw_per:.0f}"],
            ["rss per path() object, store included", f"{total:.0f}"],
            ["  of which the store (table + slots), per interned path", f"{store:.0f}"],
            ["  of which the live object", f"{total - store:.0f}"],
            ["live objects after del + gc", f"{live:.0f}"]]
    print(f"\n## 3. memory, {N:,} paths (each variant in a fresh process)\n")
    print(tabulate(rows, headers=["measure", "bytes / count"], tablefmt="github"))
    return {"rss_per_raw_file": raw_per, "rss_per_path_object_total": total, "store_bytes_per_path": store,
            "live_after_del": live}


def main():
    strs = corpus(N)
    print(f"engines {[e.name for e in pathlike.ENGINES]}")
    sections = {"construct": bench_construct(strs), "derived": bench_derived(strs), "memory": bench_memory()}
    if wants_save():
        print(f"\nRun recorded -> {save('path_store', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
