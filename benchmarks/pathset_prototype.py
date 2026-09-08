"""PathSet prototype benchmarks: many paths as one table vs. one object per path.

Sections:

1. **build** — PathSet(list) vs a list of pygim.path objects vs a list of
   pathlib.PurePath objects, at 1M and 10M paths, with memory per path.
2. **access** — per-element cost through a set (fresh objects, the reusing
   scan() cursor, .name and os.fspath) vs a list of path objects.
3. **filter** — filter_suffix / filter_name on the table vs a Python loop over
   file objects vs str.endswith vs polars over the rendered list.
4. **membership + algebra** — `x in set`, |, &, - (shared table and across
   tables) vs Python sets of file objects.
5. **render** — to_list() (the bridge to everything else) vs rendering a
   PurePath list.

Run:  python benchmarks/pathset_prototype.py [--no-save] [--small]

``--small`` stops at 1M (the 10M build needs ~3 GB for the pathlib baseline).
Each run appends its raw measurements + environment metadata to
``results/pathset_prototype.jsonl`` (see ``_results.py``).
"""

import gc
import os
import pathlib
import sys

from tabulate import tabulate

import pygim
from pygim import pathlike
from _results import save, wants_save

from _bench import REPS, best, corpus, ns, rss_mb

path = pygim.path   # bound once: the lazy top-level export is not what is measured


def ns(seconds, n):   # noqa: F811 — the module-level n-less spelling used below
    return seconds / n * 1e9


# ── 1. build ──────────────────────────────────────────────────────────────
def bench_build(sizes):
    rows, raw = [], {}
    for n in sizes:
        strs = corpus(n)
        m0 = rss_mb()
        t, ps = best(lambda: pathlike.PathSet(strs), reps=1)
        st = ps.stats()
        ps_bytes = (st["table_bytes"] + st["member_bytes"]) / n
        raw[f"pathset_{n}"] = {"seconds": t, "bytes_per_path": ps_bytes, "rows": st["rows"], "segments": st["segments"]}
        rows.append([f"{n:,}", "PathSet(list)", f"{ns(t, n):,.0f}", f"{n / t / 1e6:.2f}", f"{ps_bytes:.0f} (exact)"])
        del ps
        gc.collect()
        m0 = rss_mb()
        t, objs = best(lambda: [pathlib.PurePath(s) for s in strs], reps=1)
        pl_bytes = (rss_mb() - m0) * 2**20 / n
        raw[f"purepath_{n}"] = {"seconds": t, "bytes_per_path": pl_bytes}
        rows.append([f"{n:,}", "[PurePath(s)]", f"{ns(t, n):,.0f}", f"{n / t / 1e6:.2f}", f"{pl_bytes:.0f} (rss, unparsed)"])
        del objs
        gc.collect()
        if n <= 1_000_000:
            m0 = rss_mb()
            m0 = rss_mb()
            st = pathlike.PathStore()
            t, objs = best(lambda: [path(s, store=st) for s in strs], reps=1)
            p_bytes = (rss_mb() - m0) * 2**20 / n
            raw[f"path_{n}"] = {"seconds": t, "bytes_per_path": p_bytes}
            rows.append([f"{n:,}", "[pygim.path(s, store=st)]  (handles + table)", f"{ns(t, n):,.0f}", f"{n / t / 1e6:.2f}", f"{p_bytes:.0f} (rss, table incl.)"])
            del objs, st
            gc.collect()
        del strs
        gc.collect()
    print("\n## 1. build (one pass, GC off)\n")
    print(tabulate(rows, headers=["paths", "container", "ns/path", "Mpaths/s", "bytes/path"], tablefmt="github"))
    return raw


# ── 2. access through views ───────────────────────────────────────────────
def bench_access(n=1_000_000):
    strs = corpus(n)
    ps = pathlike.PathSet(strs)
    files = [path(s) for s in strs]
    rows, raw = [], {}

    def measure(label, fn, ref_label, ref):
        t, _ = best(fn)
        r, _ = best(ref)
        raw[label] = {"seconds": t, "reference_seconds": r, "reference": ref_label}
        rows.append([label, f"{ns(t, n):,.0f}", ref_label, f"{ns(r, n):,.0f}"])

    def loop_views():
        for _ in ps:
            pass

    def loop_scan():
        for _ in ps.scan():
            pass

    def loop_files():
        for _ in files:
            pass

    measure("for v in ps (fresh views)", loop_views, "for f in files", loop_files)
    measure("for v in ps.scan() (one view)", loop_scan, "for f in files", loop_files)
    measure("v.name over scan()", lambda: [v.name for v in ps.scan()], "f.name over files", lambda: [f.name for f in files])
    measure("os.fspath(v) over scan()", lambda: [os.fspath(v) for v in ps.scan()], "os.fspath(f)", lambda: [os.fspath(f) for f in files])
    measure("v.parent over scan()", lambda: [v.parent for v in ps.scan()], "f.parent", lambda: [f.parent for f in files])
    measure("hash(v) over scan()", lambda: [hash(v) for v in ps.scan()], "hash(f)", lambda: [hash(f) for f in files])
    print(f"\n## 2. per-element access, {n:,} paths (ns per element, best of {REPS})\n")
    print(tabulate(rows, headers=["PathSet", "ns", "objects", "ns"], tablefmt="github"))
    return raw, strs, ps, files


# ── 3. filters ────────────────────────────────────────────────────────────
def bench_filter(strs, ps, files):
    n = len(strs)
    rows, raw = [], {}

    def rec(label, fn):
        t, r = best(fn)
        raw[label] = {"seconds": t}
        rows.append([label, f"{t * 1e3:,.1f}", f"{ns(t, n):,.0f}", f"{len(r):,}" if hasattr(r, "__len__") else str(r)])

    rec("ps.filter_suffix('.yaml')", lambda: ps.filter_suffix(".yaml"))
    rec("ps.filter_name('file1*')", lambda: ps.filter_name("file1*"))
    rec("[f for f in files if f.suffix == '.yaml']", lambda: [f for f in files if f.suffix == ".yaml"])
    rec("[s for s in strs if s.endswith('.yaml')]", lambda: [s for s in strs if s.endswith(".yaml")])
    try:
        import polars as pl
        series = pl.Series(ps.to_list())
        rec("polars series.str.ends_with('.yaml')", lambda: series.filter(series.str.ends_with(".yaml")))
    except ImportError:
        pass
    print(f"\n## 3. value filters, {n:,} paths (best of {REPS})\n")
    print(tabulate(rows, headers=["filter", "ms", "ns/path", "hits"], tablefmt="github"))
    return raw


# ── 4. membership + algebra ───────────────────────────────────────────────
def bench_algebra(strs, ps, files):
    n = len(strs)
    rows, raw = [], {}
    probes = strs[::10][:100_000]
    fileset = set(files)
    probe_files = [path(s) for s in probes]

    def rec(label, fn, count):
        t, _ = best(fn)
        raw[label] = {"seconds": t, "count": count}
        rows.append([label, f"{t * 1e3:,.1f}", f"{ns(t, count):,.0f}"])

    rec("s in ps (str probes)", lambda: [s in ps for s in probes], len(probes))
    rec("f in ps (file probes)", lambda: [f in ps for f in probe_files], len(probes))
    rec("f in set(files)", lambda: [f in fileset for f in probe_files], len(probes))
    a = ps.filter_suffix(".yaml")
    b = ps.filter_absolute()
    rec("a | b  (shared table)", lambda: a | b, n)
    rec("a & b  (shared table)", lambda: a & b, n)
    rec("a - b  (shared table)", lambda: a - b, n)
    rec("a.count_union(b)  (popcount, no set built)", lambda: a.count_union(b), n)
    rec("a.count_intersection(b)  (popcount)", lambda: a.count_intersection(b), n)
    other = pathlike.PathSet(corpus(n, seed=2))   # another table, half overlapping names
    rec("ps | other (two tables)", lambda: ps | other, 2 * n)
    rec("ps & other (two tables)", lambda: ps & other, n)
    fa = set(f for f in files if f.suffix == ".yaml")
    fb = set(f for f in files if f.is_absolute())
    rec("set(a) | set(b)  (file objects)", lambda: fa | fb, n)
    rec("set(a) & set(b)  (file objects)", lambda: fa & fb, n)
    print(f"\n## 4. membership and set algebra, {n:,} paths (best of {REPS})\n")
    print(tabulate(rows, headers=["operation", "ms", "ns/element"], tablefmt="github"))
    return raw


# ── 5. render ─────────────────────────────────────────────────────────────
def bench_render(strs, ps):
    n = len(strs)
    rows, raw = [], {}

    def rec(label, fn):
        t, r = best(fn)
        raw[label] = {"seconds": t}
        rows.append([label, f"{t * 1e3:,.1f}", f"{ns(t, n):,.0f}"])
        return r

    rec("ps.to_list()", lambda: ps.to_list())
    rec("[os.fspath(v) for v in ps.scan()]", lambda: [os.fspath(v) for v in ps.scan()])
    rec("[str(P) for P in PurePath list] (reference render)", lambda: [str(pathlib.PurePath(s)) for s in strs])
    try:
        import polars as pl
        rec("polars.Series(ps.to_list())", lambda: pl.Series(ps.to_list()))
    except ImportError:
        pass
    print(f"\n## 5. render, {n:,} paths (best of {REPS})\n")
    print(tabulate(rows, headers=["step", "ms", "ns/path"], tablefmt="github"))
    return raw


def main():
    small = "--small" in sys.argv[1:]
    print(f"python {sys.version.split()[0]}; engines {[e.name for e in pathlike.ENGINES]}")
    sections = {"build": bench_build([1_000_000] if small else [1_000_000, 10_000_000])}
    access, strs, ps, files = bench_access()
    sections["access"] = access
    sections["filter"] = bench_filter(strs, ps, files)
    sections["algebra"] = bench_algebra(strs, ps, files)
    sections["render"] = bench_render(strs, ps)
    if wants_save():
        print(f"\nRun recorded -> {save('pathset_prototype', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
