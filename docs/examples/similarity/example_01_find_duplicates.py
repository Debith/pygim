# type: ignore
"""Finding duplicated functions and classes with ``pygim.similarity``.

An ``Index`` collects the functions, methods and classes of ONE language and
compares every one with every other of its kind. The score is deterministic --
the same input gives the same numbers on every run and machine -- and
structural: local names, docstrings, annotations and literal values never
count, while the shape of the code and the globals, attributes and calls it
uses always do.

Results are Arrow: ``pairs()`` and ``units()`` return ``pyarrow.Table``s built
in C++, one row per pair or unit, ready for pyarrow compute, polars or a
Parquet file -- no row is ever a Python object until you ask for it.

This example demonstrates:
- Building an index from source text, from a file, and from many files at once
- Results as Arrow tables, and joining pairs back to units
- All-pairs comparison, the threshold and the minimum size
- What the score ignores and what it does not
- Imports resolved: the same call spelled through different imports matches
- Classes compared through their best method pairing
- Large results: the best N only, slim columns, and streaming to Parquet
- Languages that never meet: one index per language
"""

import tempfile

import pyarrow as pa
import pyarrow.compute as pc
import pyarrow.parquet as pq

from pygim import path, similarity
from pygim.pathlike import ext

# ----------------------------------------------------------------------------
# 1. One index, every pair
# ----------------------------------------------------------------------------
#                     ┌─ language: an index holds exactly one (see LANGUAGES)
#                     ▼
ix = similarity.Index("python")
ix.add('''
def total(prices, tax):
    """Sum with tax."""
    s = 0
    for p in prices:
        s += p * (1 + tax)
    return s

def grand_total(items: list, rate: float) -> float:
    acc = 0
    for it in items:
        acc += it * (1 + rate)
    return acc

def count(items):
    return len(items)
''', origin="shop.py")
#    ▲
#    └─ origin: what pairs() reports as the file (add_file() uses the path)

pairs = ix.pairs("function")
assert isinstance(pairs, pa.Table)                # built in C++, no Python object per row
# Three functions: every one against every other, 3 * 2 / 2 rows, best first.
assert pairs.num_rows == 3
best = pairs.slice(0, 1).to_pylist()[0]           # rows become Python objects only on request
# Renamed locals, a docstring and annotations: still an exact copy.
assert (best["a_qualname"], best["b_qualname"]) == ("total", "grand_total")
assert best["exact"]
# The score is an exact integer ratio; the float is only its quotient.
assert best["score"] == 2 * best["shared"] / best["total"]

# `a` and `b` are unit ids: they join the units table (pyarrow compute, no Python loop).
units = ix.units()
assert units.column_names == [
    "id", "kind", "qualname", "origin", "line", "owner", "nodes", "weight",
]
joined = pairs.join(units.select(["id", "nodes"]), keys="a", right_keys="id")
assert joined.num_rows == 3                       # every pair found its unit
# The origin columns are dictionary-encoded: each file name is stored once.
assert pairs.column("a_origin").chunk(0).dictionary.to_pylist() == ["shop.py"]

# ----------------------------------------------------------------------------
# 2. Threshold and minimum size
# ----------------------------------------------------------------------------
#                           ┌─ threshold: report pairs scoring at least this
#                           ▼
assert ix.pairs("function", threshold=0.9).num_rows == 1
#                                ┌─ min_size: leave out units of fewer tree nodes
#                                │  (one-line accessors are identical without being
#                                │  duplicates)
#                                ▼
assert ix.pairs("function", 0.0, min_size=30).column("a_qualname").to_pylist() == ["total"]

# ----------------------------------------------------------------------------
# 3. What counts: a different global or attribute is a different function
# ----------------------------------------------------------------------------
ix2 = similarity.Index("python")
ix2.add("def f(x):\n    return math.floor(x)\n\ndef g(y):\n    return math.ceil(y)\n")
(p,) = ix2.pairs().to_pylist()
assert not p["exact"]                             # floor is not ceil
assert 0.0 < p["score"] < 1.0                     # but the shape is the same

# ----------------------------------------------------------------------------
# 4. Imports are resolved: one call, however it was imported
# ----------------------------------------------------------------------------
# Every name is resolved statically to what it stands for -- imports, aliases,
# module-level definitions, relative imports -- before anything is compared.
spellings = similarity.Index("python")
spellings.add("import os\ndef a(p):\n    return os.path.join(p, 'x')\n")
spellings.add("import os.path as osp\ndef b(p):\n    return osp.join(p, 'x')\n")
spellings.add("from os.path import join as j\ndef c(p):\n    return j(p, 'x')\n")
spellings.add("from . import util\ndef d(x):\n    return util.helper(x)\n",
              module="pkg.a")
#             ▲
#             └─ module: anchors `from . import ...`
spellings.add("from pkg.util import helper\ndef e(y):\n    return helper(y)\n", module="pkg.b")
exact = spellings.pairs(threshold=1.0, columns=["a_qualname", "b_qualname"]).to_pylist()
exact_pairs = {(r["a_qualname"], r["b_qualname"]) for r in exact}
assert exact_pairs == {("a", "b"), ("a", "c"), ("b", "c"), ("d", "e")}

# ----------------------------------------------------------------------------
# 5. Classes: header plus best method pairing
# ----------------------------------------------------------------------------
ix3 = similarity.Index("python")
ix3.add('''
class Celsius(Unit):
    symbol = "C"
    def to_kelvin(self, v):
        return v + 273.15
    def describe(self):
        return f"{self.symbol}"

class Centigrade(Unit):
    symbol = "C"
    def describe(self):
        return f"{self.symbol}"
    def to_kelvin(self, t):
        return t + 273.15
''')
(c,) = ix3.pairs("class").to_pylist()
assert (c["a_qualname"], c["b_qualname"]) == ("Celsius", "Centigrade")
# Methods are paired by the best assignment, so their order never matters.
assert c["exact"]
# A method's `owner` is its class's id.
u = ix3.units()
celsius = u.filter(pc.equal(u["qualname"], "Celsius"))["id"][0]
methods = u.filter(pc.equal(u["owner"], celsius))["qualname"].to_pylist()
assert methods == ["Celsius.to_kelvin", "Celsius.describe"]

# ----------------------------------------------------------------------------
# 6. Large results: top-N, slim columns, streams
# ----------------------------------------------------------------------------
big = similarity.Index("python")
big.add("\n".join(f"def f{i}(a, b):\n    return a * {i % 4} + b{' + n' if i % 3 else ''}\n"
                  for i in range(400)))
ranked = big.pairs()
assert ranked.num_rows == 400 * 399 // 2                     # every combination

#               ┌─ limit: keep only the best N; the sweep skips every
#               │  comparison that could not make the cut
#               ▼
top = big.pairs(limit=10)
assert top.equals(ranked.slice(0, 10))                       # exactly the head of the ranking

#                               ┌─ columns: only what you need, in this order. (a, b, score)
#                               │  is 16 bytes a row instead of 56; names join from units()
#                               ▼
slim = big.pairs(threshold=0.9, columns=["a", "b", "score"])
assert slim.column_names == ["a", "b", "score"]

#                         ┌─ batch_size: rows per batch, exact (the last shorter)
#                         ▼
reader = big.stream_pairs(batch_size=10_000)                 # a pyarrow.RecordBatchReader
# A stream is NOT ranked: batches are swept on every core while you read them,
# in a fixed order, in bounded memory -- the way to write 100M pairs to Parquet.
with tempfile.TemporaryDirectory() as tmp:
    pairs = path(tmp) / "pairs.parquet"
    with pq.ParquetWriter(pairs, reader.schema) as writer:
        for batch in reader:
            assert batch.num_rows <= 10_000
            writer.write_batch(batch)
    assert pq.read_table(pairs).num_rows == ranked.num_rows

# ----------------------------------------------------------------------------
# 7. Many files at once, one index per language
# ----------------------------------------------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    src = path(tmp)
    (src / "geo.cpp").write_text("int twice(int v) { return v * 2; }\n"
                                 "int doubled(int w) { return w * 2; }\n")
    (src / "geo.py").write_text("def twice(v):\n    return v * 2\n")
    (src / "shapes.py").write_text("def doubled(w):\n    return w * 2\n")
    (src / "broken.py").write_text("def half(:\n")

    # Files routed by extension: one PathSet of the folder, each language's files chosen by its
    # extensions (as language_of reads them), each set added to its index in one call — read,
    # parsed and fingerprinted on worker threads with the GIL released.
    found = src.pathset("*", kind="file")
    by_language = {language: found.filter(ext(*record["exts"], case_sensitive=False))
                   for language, record in similarity.LANGUAGES.items()}
    indexes = {}
    for language, files in by_language.items():
        indexes[language] = similarity.Index(language)
        left_out = indexes[language].add_files(files)       # [(path, why)] of what was not added
        if language == "python":
            assert [path(p).name for p, _ in left_out] == ["broken.py"]
    assert sorted(indexes) == ["cpp", "python"]
    (cpp_pair,) = indexes["cpp"].pairs().to_pylist()
    assert (cpp_pair["a_qualname"], cpp_pair["b_qualname"]) == ("twice", "doubled")
    assert cpp_pair["exact"]
    (py_pair,) = indexes["python"].pairs().to_pylist()       # across files: geo.py and shapes.py
    assert py_pair["exact"]

    # A file of another language is refused, never compared.
    try:
        indexes["python"].add_file(src / "geo.cpp")
    except ValueError as exc:
        assert "is a cpp file; this index holds python" in str(exc)
    else:
        raise AssertionError("a C++ file must not enter a Python index")

print("similarity example_01 OK")
