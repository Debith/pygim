"""pygim.similarity on any source tree: the field test of docs/design/similarity.md for a project
other than CPython (LLVM, measured 2026-10-25).

Each corpus is a set of directories under ROOT; each language found in it is one index (C++ with
C++, Python with Python, never mixed).

Sections, per corpus and language:

1. **parse** — files, files left out (grouped by cause), functions, classes, seconds, index MiB.
2. **ranked** — pairs(threshold=THRESHOLD, min_size=MIN_SIZE): exact duplicates, near ones, how
   many across files, seconds.
3. **top** — pairs(limit=TOP): the best TOP pairs of every function, seconds.

Run:  python benchmarks/similarity_tree.py ROOT NAME=DIR[,DIR...] [NAME=...] [--no-save]
e.g.  python benchmarks/similarity_tree.py ~/src/llvm-project llvm=llvm/lib,llvm/include clang=clang/lib
Each run appends its raw measurements + environment metadata to
``results/similarity_tree.jsonl`` (see ``_results.py``). A pygim without Index.add_files() is read
with add_file() in a loop, so the same script measures an older build.
"""

import collections
import pathlib
import subprocess
import sys
import time

import pyarrow.compute as pc
from tabulate import tabulate

from pygim import similarity
from pygim.pathlike import path
from _results import corpus, save, wants_save

THRESHOLD = 0.8
MIN_SIZE = 30
TOP = 1000


def sources(root, dirs):
    """{language: [files]} under root/dir for each dir, through pygim's own walker."""
    found = collections.defaultdict(list)
    for d in dirs:
        for p in path(str(root / d)).rglob("*"):
            f = pathlib.Path(str(p))
            if f.is_file() and (language := similarity.language_of(f)):
                found[language].append(f)
    return {language: sorted(files) for language, files in found.items()}


def cause(message):
    """A left-out file's cause, without its origin and line: "<origin>:<line>: <what>"."""
    parts = message.split(": ", 1)
    what = parts[1] if len(parts) == 2 else message
    return what if len(what) < 70 else what[:67] + "..."


def build(language, files):
    ix = similarity.Index(language)
    rejected = collections.Counter()
    t = time.perf_counter()
    if hasattr(ix, "add_files"):
        for _, message in ix.add_files(files):
            rejected[cause(message)] += 1
    else:
        for f in files:
            try:
                ix.add_file(f)
            except (ValueError, OSError) as e:
                rejected[cause(str(e))] += 1
    return ix, time.perf_counter() - t, rejected


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    root = pathlib.Path(args[0]).expanduser().resolve()
    corpora = {name: dirs.split(",") for name, dirs in (a.split("=", 1) for a in args[1:])}
    try:
        commit = subprocess.run(["git", "-C", str(root), "log", "-1", "--format=%h %cd", "--date=short"],
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = "?"
    raw = {"root": corpus(root), "commit": commit, "corpora": corpora, "threshold": THRESHOLD, "min_size": MIN_SIZE, "top": TOP}
    print(f"{root.name} {commit}; Python {sys.version.split()[0]}\n")

    parse_rows, ranked_rows, top_rows = [], [], []
    for name, dirs in corpora.items():
        for language, files in sources(root, dirs).items():
            key = f"{name}/{language}"
            ix, seconds, rejected = build(language, files)
            st = ix.stats()
            raw[f"parse/{key}"] = {"files": len(files), "rejected": dict(rejected), "functions": st["functions"],
                                   "classes": st["classes"], "seconds": seconds, "bytes": st["bytes"]}
            parse_rows.append([key, len(files), sum(rejected.values()), f"{st['functions']:,}", f"{st['classes']:,}",
                               f"{seconds:.2f}", f"{st['bytes'] / 2**20:.0f}"])
            for c, n in rejected.most_common(4):
                parse_rows.append(["", "", f"{n} x", c, "", "", ""])

            t = time.perf_counter()
            tb = ix.pairs(threshold=THRESHOLD, min_size=MIN_SIZE, columns=["a", "b", "a_origin", "b_origin", "score", "exact"])
            seconds = time.perf_counter() - t
            exact = pc.sum(tb["exact"]).as_py() or 0
            near = tb.filter(pc.invert(tb["exact"]))
            cross = pc.sum(pc.not_equal(pc.cast(near["a_origin"], "string"), pc.cast(near["b_origin"], "string"))).as_py() or 0
            raw[f"ranked/{key}"] = {"rows": tb.num_rows, "exact": exact, "near": near.num_rows, "near_cross_file": cross, "seconds": seconds}
            ranked_rows.append([key, f"{exact:,}", f"{near.num_rows:,}", f"{cross:,}", f"{seconds:.2f}"])

            t = time.perf_counter()
            top = ix.pairs(limit=TOP, columns=["a", "b", "score"])
            seconds = time.perf_counter() - t
            raw[f"top/{key}"] = {"rows": top.num_rows, "lowest": top["score"][-1].as_py() if top.num_rows else None, "seconds": seconds}
            top_rows.append([key, f"{top.num_rows:,}", f"{raw[f'top/{key}']['lowest']:.3f}" if top.num_rows else "-", f"{seconds:.2f}"])

    print("1. parse")
    print(tabulate(parse_rows, headers=["corpus", "files", "left out", "functions", "classes", "seconds", "index MiB"]))
    print(f"\n2. ranked (score >= {THRESHOLD}, >= {MIN_SIZE} nodes)")
    print(tabulate(ranked_rows, headers=["corpus", "exact", "near", "near across files", "seconds"]))
    print(f"\n3. top {TOP} pairs of every function")
    print(tabulate(top_rows, headers=["corpus", "pairs", "lowest score", "seconds"]))
    if wants_save():
        print("\nsaved:", save("similarity_tree", raw, reps=1, in_tree=False))


if __name__ == "__main__":
    main()
