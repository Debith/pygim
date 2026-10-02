"""Markdown benchmarks: pathlike's markdown engine against Python-Markdown.

Three questions, three sections:

1. **Parse and render** — over a corpus of real markdown files: the block
   parse alone (``markdown.Document(text)``), every inline resolved
   (``doc.plain``), and the whole page as HTML (``doc.html()``), against
   Python-Markdown's ``markdown.markdown(text, extensions=[fenced_code,
   tables])`` — what ``oo docs serve`` renders with today.
2. **The stop scan** — the SIMD policy this build uses (SSE2 on x86-64,
   NEON on AArch64) against the scalar reference, over the same corpus
   joined into one text: the A/B of scan.h, both compiled into the same
   extension and timed in one process.
3. **Reading files** — ``path(p).read()`` (read, UTF-8 check, block parse)
   over every file, against ``Path.read_text`` (the read alone).

Run:  python benchmarks/markdown_parse.py [--corpus DIR] [--no-save]

The default corpus is every ``*.md`` under the repository (``.git``, ``build``
and ``node_modules`` skipped). Each run appends its raw measurements and the
environment to ``results/markdown_parse.jsonl`` (``_results.py``); ``--no-save``
measures without recording.
"""

import argparse
import time
from pathlib import Path

from tabulate import tabulate

import pygim
from pygim import pathlike
from _results import save, wants_save

REPS = 5
SKIP = {".git", "build", "node_modules", "__pycache__"}


def best(fn, *args):
    """Best-of-REPS wall time in seconds (min is the least noisy estimator)."""
    times = []
    for _ in range(REPS):
        t0 = time.perf_counter()
        fn(*args)
        times.append(time.perf_counter() - t0)
    return min(times)


def corpus(root):
    return sorted(p for p in root.rglob("*.md") if p.is_file() and not SKIP & set(p.relative_to(root).parts))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, default=Path(__file__).resolve().parents[1])
    args, _ = ap.parse_known_args()
    md = pathlike.markdown
    files = corpus(args.corpus)
    texts = [p.read_text(encoding="utf-8", errors="replace") for p in files]
    size = sum(len(t.encode("utf-8")) for t in texts)
    joined = "\n".join(texts)
    print(f"corpus: {len(files)} files, {size / 1e6:.2f} MB under {args.corpus} (scan policy: {md.SCAN})\n")

    # 1. parse and render
    def parse_all():
        for t in texts:
            md.Document(t)

    def plain_all():
        for t in texts:
            md.Document(t).plain

    def html_all():
        for t in texts:
            md.Document(t).html()

    rows, parse = [], {}
    for label, fn in [("Document(text): blocks", parse_all), ("Document(text).plain: every inline", plain_all),
                      ("Document(text).html()", html_all)]:
        parse[label] = best(fn)
    try:
        import markdown as pymd

        def pymd_all():
            for t in texts:
                pymd.markdown(t, extensions=["fenced_code", "tables"])

        parse["Python-Markdown (fenced_code, tables)"] = best(pymd_all)
    except ImportError:
        print("Python-Markdown not installed: its row is skipped")
    reference = parse.get("Python-Markdown (fenced_code, tables)")
    for label, s in parse.items():
        rows.append([label, f"{s * 1e3:.1f}", f"{size / s / 1e6:.1f}", f"{reference / s:.1f}x" if reference else "-"])
    print(tabulate(rows, headers=["parse and render", "ms", "MB/s", "vs Python-Markdown"]), "\n")

    # 2. the stop scan: SIMD policy against the scalar reference, same process
    stops = md._stops(joined, "scalar", True)
    assert stops == md._stops(joined, "simd", True)
    scan = {name: best(md._stops, joined, name, True) for name in ("scalar", "simd")}
    print(tabulate([[f"{name} ({md.SCAN if name == 'simd' else 'table'})", f"{s * 1e3:.2f}", f"{size / s / 1e6:.0f}",
                     f"{scan['scalar'] / s:.2f}x"] for name, s in scan.items()],
                   headers=["stop scan", "ms", "MB/s", "vs scalar"]))
    print(f"{stops} stops: one every {size / max(stops, 1):.0f} bytes\n")

    # 3. reading files
    def read_pygim():
        for p in files:
            pygim.path(p).read()

    def read_text():
        for p in files:
            p.read_text(encoding="utf-8")

    reads = {"pygim.path(p).read()": best(read_pygim), "Path.read_text()": best(read_text)}
    print(tabulate([[k, f"{v * 1e3:.1f}", f"{v / len(files) * 1e6:.1f}"] for k, v in reads.items()],
                   headers=["reading every file", "ms", "us per file"]))

    sections = {
        "corpus": {"root": str(args.corpus), "files": len(files), "bytes": size, "stops": stops, "scan": md.SCAN},
        "parse": parse,
        "scan": scan,
        "read": reads,
    }
    if wants_save():
        print(f"\nRun recorded -> {save('markdown_parse', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
