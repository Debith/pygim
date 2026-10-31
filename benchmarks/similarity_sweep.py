"""pygim.similarity: parsing a corpus and the all-pairs sweep.

The corpus is synthetic and seeded (the same on every machine): N Python
functions and N C++ functions, each a random composition of a few statement
shapes over random local names, so some pairs are exact copies, many share
fragments and most share little.

Sections:

1. **parse** — Index.add over the whole corpus, per language.
2. **sweep** — Index.pairs("function") at threshold 0 (every one of the
   N*(N-1)/2 pairs scored and returned as one ranked pyarrow.Table) and at
   0.9 (the prefix filter compares only pairs sharing a rare subtree), per
   language.
3. **large** — LARGE one-line Python functions, LARGE*(LARGE-1)/2 pairs (72M):
   the ranked table with every column and with (a, b, score) only, the top
   1,000 (limit=), and stream_pairs() consumed batch by batch, again with
   every column and with (a, b, score). Each in a fresh process, reporting its peak RSS above the
   resident size of the built index — what each mode costs in memory, not only in time.

Run:  python benchmarks/similarity_sweep.py [--no-save]
Each run appends its raw measurements + environment metadata to
``results/similarity_sweep.jsonl`` (see ``_results.py``).
"""

import random
import subprocess
import sys
import textwrap

from tabulate import tabulate

from pygim import pathlike, similarity
from _results import save, wants_save

from _bench import REPS, best

N = 2_000
LARGE = 12_000

PY_SHAPES = [
    "    {a} = {b} + {k}",
    "    if {a} > {k}:\n        {b} = {a} * 2",
    "    for {c} in range({a}):\n        {b} += {c}",
    "    {b} = helper({a}, {k})",
    "    {a} = [{c} for {c} in {b} if {c}]",
]
CPP_SHAPES = [
    "    {a} = {b} + {k};",
    "    if ({a} > {k}) {{ {b} = {a} * 2; }}",
    "    for (int {c} = 0; {c} < {a}; ++{c}) {{ {b} += {c}; }}",
    "    {b} = helper({a}, {k});",
    "    std::vector<int> {c}{{{a}, {b}}};",
]


def corpus(shapes, header, footer, seed=1):
    rnd = random.Random(seed)
    out = []
    for i in range(N):
        names = dict(a=f"a{rnd.randrange(9)}", b=f"b{rnd.randrange(9)}", c=f"c{rnd.randrange(9)}",
                     k=rnd.randrange(3))
        body = [rnd.choice(shapes).format(**names) for _ in range(rnd.randrange(2, 7))]
        out.append(header.format(i=i, **names) + "\n".join(body) + footer.format(**names))
    return "\n".join(out)


SOURCES = {
    "python": corpus(PY_SHAPES, "def f{i}({a}, {b}):\n", "\n    return {b}\n"),
    "cpp": corpus(CPP_SHAPES, "int f{i}(int {a}, int {b}) {{\n", "\n    return {b};\n}}\n"),
}


LARGE_RUN = textwrap.dedent("""
    import sys, threading, time
    from pygim import pathlike, similarity
    from pygim.utils import rss_bytes
    ix = similarity.Index("python")
    ix.add("\\n".join(f"def f{i}(a):\\n    return a + {i % 3}\\n" for i in range(int(sys.argv[2]))))
    # The process peak never resets (parsing set it), so RSS is sampled while the mode runs.
    base, top, done = rss_bytes(), [0], threading.Event()
    def sample():
        while not done.is_set():
            top[0] = max(top[0], rss_bytes())
            time.sleep(0.002)
    sampler = threading.Thread(target=sample)
    sampler.start()
    t = time.perf_counter()
    mode = sys.argv[1]
    slim = ["a", "b", "score"] if mode.endswith("_slim") else None
    if mode.startswith("stream"):
        rows = sum(b.num_rows for b in ix.stream_pairs(columns=slim))
    elif mode == "top1000":
        rows = ix.pairs(limit=1000).num_rows
    else:
        rows = ix.pairs(columns=slim).num_rows
    seconds = time.perf_counter() - t
    done.set()
    sampler.join()
    print(pathlike.encode({"seconds": seconds, "rows": rows, "peak_rss_delta": max(top[0], rss_bytes()) - base}, engine="json"))
""")


def large():

    out = {}
    for mode in ("ranked", "ranked_slim", "top1000", "stream", "stream_slim"):
        res = subprocess.run([sys.executable, "-c", LARGE_RUN, mode, str(LARGE)],
                             capture_output=True, text=True, check=True)
        out[mode] = pathlike.decode(res.stdout, engine="json")
    return out


def main():
    rows, raw = [], {}
    for language, text in SOURCES.items():
        def build():
            ix = similarity.Index(language)
            ix.add(text)
            return ix

        t_parse, ix = best(build)
        assert ix.units("function").num_rows == N
        t_all, everything = best(lambda: ix.pairs("function", 0.0))
        assert everything.num_rows == N * (N - 1) // 2
        t_high, high = best(lambda: ix.pairs("function", 0.9))
        raw[language] = {"parse": {"seconds": t_parse},
                         "pairs_all": {"seconds": t_all, "pairs": everything.num_rows, "result": "arrow"},
                         "pairs_0.9": {"seconds": t_high, "pairs": high.num_rows, "result": "arrow"}}
        rows.append([language, f"{t_parse * 1e3:,.0f}", f"{t_all:,.2f} ({everything.num_rows:,})",
                     f"{t_high * 1e3:,.0f} ({high.num_rows:,})"])
    print(f"{N:,} functions per language, best of {REPS}")
    print(tabulate(rows, headers=["language", "parse ms", "pairs(0) s (pairs)", "pairs(0.9) ms (pairs)"]))
    raw["large"] = large()
    print(f"\n{LARGE:,} functions, {LARGE * (LARGE - 1) // 2:,} pairs (python), fresh process each")
    print(tabulate([[m, f"{r['rows']:,}", f"{r['seconds']:.2f}", f"{r['peak_rss_delta'] / 2**20:,.0f}"]
                    for m, r in raw["large"].items()],
                   headers=["mode", "rows", "seconds", "peak RSS MiB"]))
    if wants_save():
        print("saved:", save("similarity_sweep", raw, reps=REPS))


if __name__ == "__main__":
    main()
