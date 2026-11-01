"""pygim.similarity on a CPython checkout: the field test of docs/design/similarity.md.

Three corpora — the standard library (Lib/ without tests), Lib's tests, and the
C core (Objects, Python, Modules, Parser, Include) — each one index, never mixed.

Sections:

1. **parse** — files read, files rejected (grouped by cause), functions,
   classes, seconds, index bytes.
2. **sweep** — stream_pairs() over every pair of the stdlib and the C core:
   rows, seconds, peak RSS (sampled while it runs) and a 20-bin score histogram.
3. **ranked** — pairs(threshold=THRESHOLD, min_size=MIN_SIZE) for all three:
   exact duplicates, near-duplicates (THRESHOLD <= score < 1), how many cross
   files, how many are Argument Clinic output; written to OUT/<corpus>.parquet.
   Its peak RSS is above what the process already holds, so memory the sweep
   left allocated and the ranking reused reads as 0.
4. **sample** — per corpus, the first cross-file near-duplicate in each score
   band, to read by hand.

Run:  python benchmarks/similarity_cpython.py CPYTHON_DIR [--out DIR] [--no-save]
Each run appends its raw measurements + environment metadata to
``results/similarity_cpython.jsonl`` (see ``_results.py``). The Python
frontend is pygim.code's parser (Python 3.14's grammar), whatever Python runs
the script; the files are read, parsed and fingerprinted by one add_files()
call per corpus, on worker threads.
"""

import argparse
import collections
import functools
import operator
import subprocess
import sys
import threading
import time  # time.sleep: the RSS sampler's pause; the timing is pygim's QuickTimer

import pyarrow.compute as pc
import pyarrow.parquet as pq
from tabulate import tabulate

from pygim import path as pygim_path, similarity   # `path` names a file below
from pygim.pathlike import ext
from pygim.utils import QuickTimer, rss_bytes
from _bench import folder
from _results import corpus, save, wants_save

THRESHOLD = 0.8
MIN_SIZE = 30
TESTDIRS = ("test", "tests", "idle_test")
C_DIRS = ("Objects", "Python", "Modules", "Parser", "Include")


def corpora(root):
    """Each corpus as (language, PathSet): Lib's files with and without its test folders, and the C
    core's files whose extension is C's or C++'s, as similarity.language_of reads one."""
    lib = (root / "Lib").pathset("**/*.py", kind="file")   # sorted already
    stdlib = (root / "Lib").pathset("**/*.py", kind="file", skip=TESTDIRS)
    core = functools.reduce(operator.or_, ((root / d).pathset("**/*", kind="file") for d in C_DIRS))   # one table: a bitmap union
    return {
        "stdlib": ("python", stdlib),
        "tests": ("python", lib - stdlib),
        "c": ("cpp", core.filter(ext(*similarity.LANGUAGES["cpp"]["exts"], case_sensitive=False))),
    }


def cause(message, path, root):
    """A rejected file's cause, grouped. The message is "<origin>:<line>: <what>"
    and <what> may contain colons itself, so the known origin is cut off first."""
    rest = message[len(str(path)) + 1:] if message.startswith(str(path) + ":") else message
    line_text, _, what = rest.partition(": ")
    line = int(line_text) if line_text.isdigit() else 0
    if "unclosed" in what or "unmatched" in what:
        return f"unbalanced brackets ({what})"
    if line == 0:
        return f"other: {what}"
    try:
        src = path.read_text(errors="replace").splitlines()[line - 1].strip()
    except (IndexError, ValueError):
        src = ""
    if src.startswith("lazy "):   # 3.15's: pygim.code reads 3.14, which has t-strings and `except A, B` already
        return "newer syntax: lazy import"
    return f"other: {what}"


def build(language, files, root):
    """One index of `files`, read, parsed and fingerprinted in one add_files() call."""
    ix = similarity.Index(language)
    rejected = collections.Counter()
    examples = {}
    with QuickTimer.silent("parse") as parsing:
        for path, message in ix.add_files(files):
            f = pygim_path(path)
            c = cause(message, f, root)
            rejected[c] += 1
            examples.setdefault(c, str(f.relative_to(root)))
    return ix, parsing.total.seconds, rejected, examples


def score_bins(scores, bins):
    """Each score's bin of `bins` equal ones over [0, 1]: floor(bins * score), 1.0 in the last."""
    return pc.min_element_wise(pc.cast(pc.floor(pc.multiply(scores, float(bins))), "int64"), bins - 1)


class PeakRss:
    """RSS sampled every 10 ms above the level at entry (the process peak never resets)."""

    def __enter__(self):
        self.base, self.top, self._done = rss_bytes(), 0, threading.Event()
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()
        return self

    def _run(self):
        while not self._done.is_set():
            self.top = max(self.top, rss_bytes())
            time.sleep(0.01)

    def __exit__(self, *exc):
        self._done.set()
        self._t.join()
        self.mib = (max(self.top, rss_bytes()) - self.base) / 2**20


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("checkout", type=folder, help="a CPython checkout")
    parser.add_argument("--out", type=pygim_path, default="similarity_cpython", help="where the ranked tables are written")
    parser.add_argument("--no-save", action="store_true", help="measure only: append no record (_results.wants_save)")
    args = parser.parse_args()
    root, out = args.checkout, args.out
    out.mkdir(parents=True, exist_ok=True)
    try:
        commit = subprocess.run(["git", "-C", str(root), "log", "-1", "--format=%h %cd", "--date=short"],
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = "?"
    raw = {"checkout": corpus(root), "commit": commit, "python": sys.version.split()[0],
           "threshold": THRESHOLD, "min_size": MIN_SIZE}
    print(f"CPython {commit} at {root}; Python {raw['python']}\n")

    # 1. parse
    indexes, rows = {}, []
    for name, (language, files) in corpora(root).items():
        ix, seconds, rejected, examples = build(language, files, root)
        indexes[name] = ix
        st = ix.stats()
        raw[f"parse/{name}"] = {"files": len(files), "rejected": dict(rejected), "examples": examples,
                                "functions": st["functions"], "classes": st["classes"], "seconds": seconds,
                                "bytes": st["bytes"]}
        rows.append([name, len(files), sum(rejected.values()), f"{st['functions']:,}", f"{st['classes']:,}",
                     f"{seconds:.1f}", f"{st['bytes'] / 2**20:.1f}"])
    print("1. parse")
    print(tabulate(rows, headers=["corpus", "files", "rejected", "functions", "classes", "seconds", "index MiB"]))
    for name in indexes:
        for c, n in collections.Counter(raw[f"parse/{name}"]["rejected"]).most_common():
            print(f"   {name}: {n:4d} x {c}   e.g. {raw[f'parse/{name}']['examples'][c]}")

    # 2. sweep
    rows, sweeping = [], QuickTimer.silent("sweep")   # a lap a corpus
    for name in ("stdlib", "c"):
        hist, n = [0] * 20, 0
        with sweeping.lap(name) as lap, PeakRss() as rss:
            for b in indexes[name].stream_pairs(columns=["score"]):
                for v in pc.value_counts(score_bins(b.column(0), 20)).to_pylist():
                    hist[v["values"]] += v["counts"]
                n += b.num_rows
        seconds = lap.took.seconds
        raw[f"sweep/{name}"] = {"rows": n, "seconds": seconds, "peak_rss_mib": rss.mib, "hist": hist}
        rows.append([name, f"{n:,}", f"{seconds:.1f}", f"{rss.mib:,.0f}", " ".join(f"{x:,}" for x in hist[15:])])
    print("\n2. sweep (every pair, streamed)")
    print(tabulate(rows, headers=["corpus", "pairs", "seconds", "peak RSS MiB", "histogram 0.75..1.00 (5 bins)"]))

    # 3. ranked
    rows, ranked, ranking = [], {}, QuickTimer.silent("ranked")   # a lap a corpus
    for name, ix in indexes.items():
        with ranking.lap(name) as lap, PeakRss() as rss:
            tb = ix.pairs(threshold=THRESHOLD, min_size=MIN_SIZE)
        seconds = lap.took.seconds
        pq.write_table(tb, str(out / f"{name}.parquet"))
        ranked[name] = tb
        exact = pc.sum(tb["exact"]).as_py() or 0
        near = tb.filter(pc.invert(tb["exact"]))
        a_o = pc.cast(near["a_origin"], "string")
        b_o = pc.cast(near["b_origin"], "string")
        cross = pc.sum(pc.not_equal(a_o, b_o)).as_py() or 0
        clinic = pc.sum(pc.and_(pc.match_substring(a_o, "/clinic/"), pc.match_substring(b_o, "/clinic/"))).as_py() or 0
        raw[f"ranked/{name}"] = {"rows": tb.num_rows, "exact": exact, "near": near.num_rows, "near_cross_file": cross,
                                 "near_clinic": clinic, "seconds": seconds, "peak_rss_mib": rss.mib}
        rows.append([name, f"{exact:,}", f"{near.num_rows:,}", f"{cross:,}", f"{clinic:,}", f"{seconds:.1f}", f"{rss.mib:,.0f}"])
    print(f"\n3. ranked (score >= {THRESHOLD}, >= {MIN_SIZE} nodes; parquet in {out}/)")
    print(tabulate(rows, headers=["corpus", "exact", "near", "near across files", "near, clinic", "seconds", "peak RSS MiB"]))

    # 4. sample
    print("\n4. sample: first cross-file near-duplicate per band (clinic output left out)")
    rel = lambda o: str(p.relative_to(root)) if (p := pygim_path(o)).is_relative_to(root) else o   # never a text cut (#129)
    for name, tb in ranked.items():
        rows = [r for r in tb.filter(pc.invert(tb["exact"])).to_pylist()
                if r["a_origin"] != r["b_origin"] and "/clinic/" not in r["a_origin"]]
        for lo, hi in ((0.95, 1.0), (0.9, 0.95), (0.85, 0.9), (THRESHOLD, 0.85)):
            r = next((r for r in rows if lo <= r["score"] < hi), None)
            if r:
                print(f"   {name:6s} {r['score']:.2f}  {r['a_qualname']}  {rel(r['a_origin'])}:{r['a_line']}")
                print(f"   {'':6s}       {r['b_qualname']}  {rel(r['b_origin'])}:{r['b_line']}")

    if wants_save():
        print("\nsaved:", save("similarity_cpython", raw, reps=1, in_tree=False))


if __name__ == "__main__":
    main()
