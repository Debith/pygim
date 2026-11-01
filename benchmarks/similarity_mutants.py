"""pygim.similarity: does the score tell near-duplicates from unrelated code?

Real functions (30-400 AST nodes, module level) from a Python standard
library, seeded sample of 600. Each gets ONE structural edit — flip an
operator, rename an attribute, drop a statement, duplicate a statement — and
is scored against its original; 1,000 random pairs of different functions are
the baseline. A good score keeps the edited copies high and the unrelated
pairs low.

Sections:

1. **unrelated** — median and 99th percentile of the random pairs.
2. **one edit** — per edit kind: count, median, 10th percentile, minimum.

Run:  python benchmarks/similarity_mutants.py [STDLIB_DIR] [--no-save]
STDLIB_DIR defaults to the running interpreter's standard library. Each run
appends its raw measurements + environment metadata to
``results/similarity_mutants.jsonl`` (see ``_results.py``).
"""

import argparse
import ast  # edits and unparses the stdlib's functions: pygim.code reads trees, writes no source
import copy
import random
import statistics
import sys
import sysconfig

import pyarrow.compute as pc
from tabulate import tabulate

from pygim import similarity
from _bench import folder
from _results import corpus, save, wants_save

SAMPLE = 600
TESTDIRS = ("test", "tests", "idle_test")
UNRELATED = 1_000
EDITS = ("flip operator", "rename attribute", "drop statement", "duplicate statement")


def functions(root):
    out = []
    for content in root.pathset("**/*.py", kind="file", skip=TESTDIRS).read_each():   # sorted already, in pathlib's order
        try:
            tree = ast.parse(bytes(content))
        except (SyntaxError, ValueError):
            continue
        out += [n for n in tree.body if isinstance(n, ast.FunctionDef) and 30 <= sum(1 for _ in ast.walk(n)) <= 400]
    random.Random(7).shuffle(out)
    return out[:SAMPLE]


def mutate(fn, how, rnd):
    m = copy.deepcopy(fn)
    if how == "flip operator":
        ops = [n for n in ast.walk(m) if isinstance(n, (ast.BinOp, ast.Compare))]
        if not ops:
            return None
        o = rnd.choice(ops)
        if isinstance(o, ast.BinOp):
            o.op = ast.Sub() if not isinstance(o.op, ast.Sub) else ast.Add()
        else:
            o.ops[0] = ast.NotEq() if not isinstance(o.ops[0], ast.NotEq) else ast.Eq()
    elif how == "rename attribute":
        attrs = [n for n in ast.walk(m) if isinstance(n, ast.Attribute)]
        if not attrs:
            return None
        rnd.choice(attrs).attr += "_x"
    elif how == "drop statement":
        bodies = [n.body for n in ast.walk(m) if isinstance(getattr(n, "body", None), list) and len(n.body) > 1]
        if not bodies:
            return None
        b = rnd.choice(bodies)
        b.pop(rnd.randrange(len(b)))
    else:
        bodies = [n.body for n in ast.walk(m) if isinstance(getattr(n, "body", None), list)]
        b = rnd.choice(bodies)
        i = rnd.randrange(len(b))
        b.insert(i, copy.deepcopy(b[i]))
    return m


def score(a, b):
    """The score of the two module-level functions themselves (their nested
    functions and classes are units too, and are not what is measured)."""
    ix = similarity.Index("python")
    ix.add(ast.unparse(a))
    second = len(ix)
    ix.add(ast.unparse(b))
    t = ix.pairs(columns=["a", "b", "score"])
    return t.filter(pc.and_(pc.equal(t["a"], 0), pc.equal(t["b"], second)))["score"][0].as_py()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    try:
        root = folder(args[0] if args else sysconfig.get_paths()["stdlib"])
    except argparse.ArgumentTypeError as e:
        sys.exit(str(e))
    fns = functions(root)
    rnd = random.Random(7)
    unrelated = sorted(score(fns[i], fns[j]) for i, j in
                       ((rnd.randrange(len(fns)), rnd.randrange(len(fns))) for _ in range(UNRELATED)) if i != j)
    raw = {"stdlib": corpus(root), "functions": len(fns),
           "unrelated": {"median": statistics.median(unrelated), "p99": unrelated[int(0.99 * len(unrelated))]}}
    rows = [["unrelated pairs", len(unrelated), f"{raw['unrelated']['median']:.2f}", "", f"p99 {raw['unrelated']['p99']:.2f}"]]
    for k, how in enumerate(EDITS):
        r = random.Random(1000 + k)
        s = sorted(score(f, m) for f in fns if (m := mutate(f, how, r)) is not None)
        raw[how] = {"n": len(s), "median": statistics.median(s), "p10": s[len(s) // 10], "min": s[0]}
        rows.append([f"one edit: {how}", len(s), f"{raw[how]['median']:.2f}", f"{raw[how]['p10']:.2f}", f"min {s[0]:.2f}"])
    print(f"{len(fns)} functions from {root}")
    print(tabulate(rows, headers=["pairs", "n", "median", "p10", "tail"]))
    if wants_save():
        print("saved:", save("similarity_mutants", raw, reps=1))


if __name__ == "__main__":
    main()
