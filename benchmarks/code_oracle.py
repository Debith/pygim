"""pygim.code against CPython's own parser: every file of a tree, and token-level mutants of them.

Sections:

1. **files** — code.parse(root) once, then each tree's dump(include_attributes=True) against
   ast.dump(ast.parse(bytes), include_attributes=True) from python3.14, file by file. Reported:
   same, differ, refused by one side only — each disagreement with the first place it differs.
2. **mutants** — N copies of small files with one token deleted, inserted, replaced or two
   swapped; python3.14 says which are Python, and pygim.code must say the same of each.
3. **time** — code.parse's wall time for the tree, and python3.14's ast.parse of the same files
   on one thread, for scale.

Run:  PYTHONPATH=src python benchmarks/code_oracle.py ROOT [--mutants N] [--seed S] [--no-save]
python3.14 must be on PATH: the parser targets its grammar, and its ast module is the oracle.
Measured 2026-10-26 on CPython 3.14.3's Lib (1,846 files): 1,838 the same with positions; 4
differ (2 hold identifiers NFKC changes, 2 only in repr's Unicode version), 4 refused by both;
5,000 of 5,000 mutants judged as CPython judges them. Records go to ``results/code_oracle.jsonl``
(see ``_results.py``).
"""

import argparse
import random
import re
import subprocess
import sys
import tempfile
import time

from pygim import code, path, pathlike
from _results import save, wants_save

# What python3.14 runs: for each path on stdin, one JSON line {"ok": bool, "dump"|"error": ...};
# with --verdicts, only whether each source is Python; with --time, the seconds ast.parse took.
CPYTHON = r"""
import ast, json, sys, time, warnings
warnings.simplefilter("ignore")
mode = sys.argv[1]
total = 0.0
for line in sys.stdin:
    path = line.rstrip("\n")
    src = open(path, "rb").read()
    t = time.perf_counter()
    try:
        tree = ast.parse(src)
        total += time.perf_counter() - t
        out = {"ok": True, "dump": ast.dump(tree, include_attributes=True) if mode == "dump" else ""}
    except (SyntaxError, ValueError, MemoryError, RecursionError) as e:
        out = {"ok": False, "error": str(e)}
    print(json.dumps(out), flush=True)
print(json.dumps({"seconds": total}))
"""


def cpython(paths, mode):
    """python3.14's answer for each path, and the seconds its ast.parse took in all."""
    done = subprocess.run(["python3.14", "-c", CPYTHON, mode], input="\n".join(map(str, paths)) + "\n",
                          capture_output=True, text=True, check=True)
    lines = pathlike.decode(done.stdout, engine="jsonl")
    return lines[:-1], lines[-1]["seconds"]


def first_difference(a, b):
    i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
    return a[max(0, i - 80):i + 80], b[max(0, i - 80):i + 80]


def section_files(root):
    t = time.perf_counter()
    corpus = code.parse(root)
    took = time.perf_counter() - t
    paths = corpus.files
    theirs, cpython_seconds = cpython(paths, "dump")
    counts = {"same": 0, "differ": 0, "only pygim refuses": 0, "only cpython refuses": 0, "both refuse": 0}
    shown = []
    for i, (path, answer) in enumerate(zip(paths, theirs)):
        try:
            ours = corpus[i].dump(include_attributes=True)
        except SyntaxError as e:
            ours = None
            why = str(e)
        if ours is None:
            key = "both refuse" if not answer["ok"] else "only pygim refuses"
            if answer["ok"]:
                shown.append((path, why, ""))
        elif not answer["ok"]:
            key = "only cpython refuses"
            shown.append((path, "", answer["error"]))
        elif ours == answer["dump"]:
            key = "same"
        else:
            key = "differ"
            shown.append((path, *first_difference(ours, answer["dump"])))
        counts[key] += 1
    return {"counts": counts, "seconds": took, "cpython_seconds": cpython_seconds, "stats": corpus.stats()}, shown


MUTATIONS = ["(", ")", "[", "]", "{", "}", ":", ",", "=", "*", "**", ".", "if", "else", "for", "in", "lambda", "yield",
             "not", "async", "await", "def", "class", "return", "x", "1", '"s"', 'f"{x}"', "->", ":=", "@", "import",
             "from", "as", "with", "try", "except", "finally", "match", "case", "type", "\n", "\\\n", "    ", "\t", "#",
             "!", "/", "|", ";"]


def mutant_of(text, rng):
    """One token of `text` deleted, inserted before, replaced, or swapped with another (a word or punctuation)."""
    spans = [m.span() for m in re.finditer(r"\w+|[^\w\s]+|\n", text)]
    a, b = rng.choice(spans)
    op = rng.randrange(4)
    if op == 0:
        return text[:a] + text[b:]
    if op == 1:
        return text[:a] + rng.choice(MUTATIONS) + " " + text[a:]
    if op == 2:
        return text[:a] + rng.choice(MUTATIONS) + text[b:]
    c, d = rng.choice(spans)
    if c < a:
        a, b, c, d = c, d, a, b
    if b > c:
        return text[:a] + text[b:]
    return text[:a] + text[c:d] + text[b:c] + text[a:b] + text[d:]


def section_mutants(root, count, seed):
    rng = random.Random(seed)
    small = [p for p in path(root).rglob("*.py") if 200 < p.size() < 6000]
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for n in range(count):
            text = rng.choice(small).read_text(errors="surrogateescape", newline="")   # bytes kept as they are
            p = path(tmp) / f"m{n:06d}.py"
            p.write_text(mutant_of(text, rng), errors="surrogateescape", newline="")
            paths.append(p)
        theirs, _ = cpython(paths, "verdicts")
        agree, shown = 0, []
        for p, answer in zip(paths, theirs):
            try:
                code.parse_source(p.read_bytes(), str(p))
                ours = True
            except SyntaxError:
                ours = False
            if ours == answer["ok"]:
                agree += 1
            elif len(shown) < 20:
                shown.append((p.read_bytes()[:300], ours, answer.get("error", "")))
    return {"count": count, "agree": agree}, shown


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--mutants", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-save", action="store_true")
    args = ap.parse_args()

    files, shown = section_files(args.root)
    print(f"files: {files['counts']}")
    print(f"time: pygim.code {files['seconds'] * 1000:.1f} ms for {files['stats']['files']} files "
          f"({files['stats']['bytes'] / 1e6:.1f} MB, {files['stats']['workers']} workers); "
          f"python3.14 ast.parse {files['cpython_seconds'] * 1000:.1f} ms on one thread")
    for path, ours, theirs in shown[:30]:
        print(f"  {path}\n    pygim : {ours}\n    python: {theirs}")
    mutants, wrong = section_mutants(args.root, args.mutants, args.seed)
    print(f"mutants: {mutants['agree']} of {mutants['count']} judged as CPython judges them")
    for text, ours, why in wrong:
        print(f"  pygim {'accepts' if ours else 'refuses'}; python: {why or 'accepts'}\n    {text!r}")
    if wants_save():
        print("saved to", save("code_oracle", {"files": files, "mutants": mutants}, reps=1))


if __name__ == "__main__":
    sys.exit(main())
