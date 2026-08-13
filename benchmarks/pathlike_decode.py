"""pathlike decode benchmarks.

Three questions, three sections:

1. **Decode throughput** — pygim.path().read() vs the ecosystem parsers
   (PyYAML with libyaml, stdlib json, stdlib tomllib) on config-shaped and
   records-shaped documents.
2. **Thread scaling** — wall-clock speedup of parallel reads (the GIL is
   released during I/O + parse; materialisation still serialises on it).
3. **Key-cache sweep** — read(key_cache=N) across cache sizes x key shapes
   (short / long / unicode / all-distinct keys).

Run:  python benchmarks/pathlike_decode.py [--no-save]

Each run appends its raw measurements + environment metadata to
``results/pathlike_decode.jsonl`` (see ``_results.py``), so performance can be
followed across commits; ``--no-save`` measures without recording.
"""

import json
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from tabulate import tabulate

import pygim
from _results import save, wants_save

REPS = 7


def best(fn, *args):
    """Best-of-REPS wall time in seconds (min is the least noisy estimator)."""
    times = []
    for _ in range(REPS):
        t0 = time.perf_counter()
        fn(*args)
        times.append(time.perf_counter() - t0)
    return min(times)


# ── Workloads ────────────────────────────────────────────────────────────────

def records(n, key_maker):
    return [{key_maker(k): (i if k % 2 else f"value-{i}") for k in range(8)} for i in range(n)]


def make_files(tmp):
    """One decode workload per (format x shape); return {label: (file, oracle_fn)}.

    The files are written by pygim itself — ``path(...).write(content)`` is
    format-universal, so a single loop covers yaml, json and toml. The only
    format-specific fact left is which reference parser to race (chosen by
    suffix), and that TOML roots must be mappings (TOML documents are tables).
    """
    import tomllib
    import yaml

    oracles = {
        ".yaml": lambda text: yaml.load(text, Loader=yaml.CSafeLoader),
        ".json": json.loads,
        ".toml": tomllib.loads,
    }

    config = {
        "server": {"host": "db.example.com", "port": 5432, "opts": {"tls": True, "retry": 3}},
        "features": [f"feature-{i}" for i in range(50)],
        "limits": {f"limit_{i}": i * 1.5 for i in range(50)},
    }
    rows = records(20_000, lambda k: f"key{k}")
    workloads = {
        "config.yaml": config,
        "config.json": config,
        "config.toml": config,
        "records.yaml": rows,
        "records.json": rows,
        "records.toml": {"rows": rows},   # mapping root, as TOML requires
    }

    out = {}
    for name, content in workloads.items():
        f = pygim.path(tmp / name)
        f.write(content)
        label = f"{name} ({f.size() / 1024:,.0f} KB)"
        oracle = oracles[f.suffix]
        out[label] = (f, lambda fp, o=oracle: o(Path(fp).read_text()))
    return out


# ── 1. decode throughput ─────────────────────────────────────────────────────

def bench_throughput(files):
    rows = []
    for label, (fpath, oracle) in files.items():
        ours = best(lambda: pygim.path(fpath).read())
        ref = best(lambda: oracle(fpath))
        assert pygim.path(fpath).read() == oracle(fpath), f"decode mismatch on {label}"
        rows.append({"workload": label, "pathlike_s": ours, "reference_s": ref})

    table = [[r["workload"], f"{r['pathlike_s'] * 1e3:8.2f}", f"{r['reference_s'] * 1e3:8.2f}",
              f"{r['reference_s'] / r['pathlike_s']:5.1f}x"] for r in rows]
    print("\n== Decode throughput (best of %d) ==" % REPS)
    print(tabulate(table, headers=["workload", "pathlike ms", "reference ms", "speedup"],
                   tablefmt="github"))
    return rows


# ── 2. thread scaling ────────────────────────────────────────────────────────

def bench_threads(tmp):
    rows_data = records(4_000, lambda k: f"key{k}")
    files = []
    for i in range(32):
        p = tmp / f"scale{i}.json"
        p.write_text(json.dumps(rows_data))
        files.append(pygim.path(p))

    def read_all(workers):
        with ThreadPoolExecutor(max_workers=workers) as ex:
            list(ex.map(lambda f: f.read(), files))

    # One measurement per worker count; time and speedup derive from the SAME run.
    rows = [{"threads": w, "wall_s": best(read_all, w)} for w in (1, 2, 4, 8)]

    base = rows[0]["wall_s"]
    table = [[r["threads"], f"{r['wall_s'] * 1e3:8.1f}", f"{base / r['wall_s']:4.2f}x"]
             for r in rows]
    print("\n== Thread scaling: 32 x 1.2MB json, GIL released during parse ==")
    print(tabulate(table, headers=["threads", "wall ms", "speedup"], tablefmt="github"))
    return rows


# ── 3. key-cache sweep ───────────────────────────────────────────────────────

def bench_key_cache(tmp):
    shapes = {
        "short keys (8 x 4ch)": lambda k: f"key{k}",
        "long keys (8 x 48ch)": lambda k: f"key{k}" + "x" * 43,
        "unicode keys (8)": lambda k: f"avain_{k}_hätä_åäö",
    }
    n = 20_000
    paths = {}
    for label, mk in shapes.items():
        p = tmp / f"cache_{len(paths)}.json"
        p.write_text(json.dumps(records(n, mk)))
        paths[label] = p
    # worst case: every key distinct -> the cache can only cost, never pay
    pd = tmp / "cache_distinct.json"
    pd.write_text(json.dumps([{f"k{i}_{k}": i for k in range(8)} for i in range(n)]))
    paths["distinct keys (160k)"] = pd

    sizes = [0, 8, 64, 256, 4096, -1]
    rows = []
    for label, p in paths.items():
        f = pygim.path(p)
        rows.append({"workload": label,
                     "seconds_by_cache": {str(s): best(lambda: f.read(key_cache=s))
                                          for s in sizes}})

    table = []
    for r in rows:
        base = r["seconds_by_cache"]["0"]
        row = [r["workload"]]
        for s in sizes:
            t = r["seconds_by_cache"][str(s)]
            delta = f" ({(base - t) / base:+4.0%})" if s != 0 else ""
            row.append(f"{t * 1e3:7.1f}{delta}")
        table.append(row)
    print(f"\n== Key-cache sweep: ms per read of {n} records (Δ vs cache off) ==")
    print(tabulate(table, headers=["workload"] + [f"cache={s}" for s in sizes],
                   tablefmt="github"))
    return rows


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        sections = {
            "throughput": bench_throughput(make_files(tmp)),
            "threads": bench_threads(tmp),
            "key_cache": bench_key_cache(tmp),
        }
    if wants_save():
        print(f"\nRun recorded -> {save('pathlike_decode', sections, reps=REPS)}")
