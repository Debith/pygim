"""Markdown benchmarks: pathlike's markdown engine against Python-Markdown.

Four questions, four sections:

1. **Parse and render** — over a corpus of real markdown files: the block
   parse alone (``markdown.Document(text)``), every inline resolved
   (``doc.plain``), and the whole page as HTML (``doc.html()``), against
   Python-Markdown's ``markdown.markdown(text, extensions=[fenced_code,
   tables])`` — what ``oo docs serve`` renders with today.
2. **The stop scan** — the SIMD policy this build uses (SSE2 on x86-64,
   NEON on AArch64) against the scalar reference, over the same corpus
   joined into one text: the A/B of scan.h, both compiled into the same
   extension and timed in one process. It needs the probes a release does
   not export: build with ``PYGIM_MARKDOWN_PROBES=1`` (setup.py), else the
   section is skipped and says so.
3. **Reading files** — ``path.read()`` (read, UTF-8 check, block parse)
   over every file, against ``Path.read_text`` (the read alone).
4. **Hostile shapes** — inputs built to make a parser run away (CommonMark's
   pathological cases and the ones the PR #37 review found), each at one size;
   tests/unittests/test_pathlike_markdown_hostile.py checks how they grow.

Run:  python benchmarks/markdown_parse.py [--corpus DIR] [--no-save]

The default corpus is every ``*.md`` under the repository (``.git``, ``build``
and ``node_modules`` skipped), walked and read through pygim's own path and
PathSet — so the texts are the files' exact bytes, CRLF included. Each run
appends its raw measurements and the environment to
``results/markdown_parse.jsonl`` (``_results.py``); ``--no-save`` measures
without recording.
"""

import argparse
import time
from pathlib import Path   # only for the Path.read_text reference row in section 3

from tabulate import tabulate

import pygim
from pygim import pathlike
from _results import save, wants_save

REPS = 5
SKIP = {".git", "build", "node_modules", "__pycache__"}
md = pathlike.markdown


def best(fn, *args):
    """Best-of-REPS wall time in seconds (min is the least noisy estimator)."""
    times = []
    for _ in range(REPS):
        t0 = time.perf_counter()
        fn(*args)
        times.append(time.perf_counter() - t0)
    return min(times)


def corpus(root):
    """Every *.md file under root, in sorted order, outside the skipped folders."""
    depth = len(root.parts)
    return [p for p in root.rglob("*.md") if p.is_file() and not SKIP & set(p.parts[depth:])]


def table(rows, headers):
    print(tabulate(rows, headers=headers), "\n")


def parse_and_render(texts, size):
    def parse_all():
        for t in texts:
            md.Document(t)

    def plain_all():
        for t in texts:
            md.Document(t).plain

    def html_all():
        for t in texts:
            md.Document(t).html()

    runs = {
        "Document(text): blocks": parse_all,
        "Document(text).plain: every inline": plain_all,
        "Document(text).html()": html_all,
    }
    try:
        import markdown as pymd

        def pymd_all():
            for t in texts:
                pymd.markdown(t, extensions=["fenced_code", "tables"])

        runs["Python-Markdown (fenced_code, tables)"] = pymd_all
    except ImportError:
        print("Python-Markdown not installed: its row is skipped")
    parse = {label: best(fn) for label, fn in runs.items()}
    reference = parse.get("Python-Markdown (fenced_code, tables)")
    rows = []
    for label, s in parse.items():
        speedup = f"{reference / s:.1f}x" if reference else "-"
        rows.append([label, f"{s * 1e3:.1f}", f"{size / s / 1e6:.1f}", speedup])
    table(rows, ["parse and render", "ms", "MB/s", "vs Python-Markdown"])
    return parse


def stop_scan(joined, size):
    if not hasattr(md, "_stops"):   # only in a PYGIM_MARKDOWN_PROBES=1 build
        print("stop scan: skipped — build with PYGIM_MARKDOWN_PROBES=1 to compare the SIMD and scalar policies\n")
        return {}, None
    stops = md._stops(joined, "scalar", True)
    assert stops == md._stops(joined, "simd", True)
    scan = {name: best(md._stops, joined, name, True) for name in ("scalar", "simd")}
    rows = []
    for name, s in scan.items():
        policy = md._scan if name == "simd" else "table"
        rows.append([f"{name} ({policy})", f"{s * 1e3:.2f}", f"{size / s / 1e6:.0f}", f"{scan['scalar'] / s:.2f}x"])
    table(rows, ["stop scan", "ms", "MB/s", "vs scalar"])
    print(f"{stops} stops: one every {size / max(stops, 1):.0f} bytes\n")
    return scan, stops


def reading(files):
    def read_pygim():
        for p in files:
            p.read()

    def read_text():
        for p in files:
            Path(p).read_text(encoding="utf-8")

    reads = {"path.read()": best(read_pygim), "Path.read_text()": best(read_text)}
    rows = [[k, f"{v * 1e3:.1f}", f"{v / len(files) * 1e6:.1f}"] for k, v in reads.items()]
    table(rows, ["reading every file", "ms", "us per file"])
    return reads


# Each shape: the input, and what is timed on it.
HOSTILE = {
    "30,000 link definitions, then html()": (
        "".join(f"[d{i}]: /u{i}\n" for i in range(30_000)), lambda t: md.Document(t).html()),
    "4,000 duplicate headings, then sections": ("## Usage\n\n" * 4_000, lambda t: md.Document(t).sections),
    "80 KB line of nested list markers": ("- " * 40_000 + "a\n", md.Document),
    "500 unmatched backtick runs of 64+": (
        "".join("`" * (64 + i) + "!" for i in range(500)), lambda t: md.Document(t).plain),
    "50,000 nested quotes, then plain": (">" * 50_000 + " a\n", lambda t: md.Document(t).plain),
    "20,000 nested emphasis pairs, then html()": (
        "*a **a " * 20_000 + "b** b*" * 20_000, lambda t: md.Document(t).html()),
}


def hostile_shapes():
    times = {name: best(run, text) for name, (text, run) in HOSTILE.items()}
    rows = [[name, f"{len(HOSTILE[name][0]) / 1e3:.0f}", f"{s * 1e3:.1f}"] for name, s in times.items()]
    table(rows, ["hostile shape", "KB", "ms"])
    return times


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=pygim.path, default=pygim.path(__file__).resolve().parents[1])
    args, _ = ap.parse_known_args()
    files = corpus(args.corpus)
    texts = pathlike.PathSet(files).read_all_files()
    size = sum(len(t.encode("utf-8")) for t in texts)
    print(f"corpus: {len(files)} files, {size / 1e6:.2f} MB under {args.corpus}\n")

    parse = parse_and_render(texts, size)
    scan, stops = stop_scan("\n".join(texts), size)
    reads = reading(files)
    hostile = hostile_shapes()

    sections = {
        "corpus": {"root": str(args.corpus), "files": len(files), "bytes": size, "stops": stops,
                   "scan": md._scan if scan else None},
        "parse": parse,
        "scan": scan,
        "read": reads,
        "hostile": hostile,
    }
    if wants_save():
        print(f"Run recorded -> {save('markdown_parse', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
