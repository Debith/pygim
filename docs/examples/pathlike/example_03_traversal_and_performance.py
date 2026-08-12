# type: ignore
"""Directory traversal and the performance knobs.

``file`` walks directories like pathlib -- ``iterdir()``, ``glob()``,
``rglob()`` -- with two pygim twists: results are sorted and deduplicated
(deterministic across platforms), and every result inherits the engine pin
of the path that produced it. ``pathset(pattern)`` bridges the results into
``pygim.pathset.PathSet`` for set algebra.

On the performance side:

- ``read()`` releases the GIL during file I/O and parsing, so reads from a
  thread pool overlap instead of serialising.
- ``read(key_cache=N)`` bounds the per-read key-interning cache: documents
  with repeated mapping keys (arrays of records) reuse one Python string
  per distinct key. 0 disables, -1 unbounds, default 256.

This example demonstrates:
- glob patterns: ``*`` / ``?`` within a component, ``**`` across directories
- Engine-pin inheritance through traversal results
- The PathSet bridge
- Correct parallel reads through a ThreadPoolExecutor
- key_cache semantics: capacity is an optimisation, never a meaning change
"""

import json
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

for name, text in {
    "a.yaml": "k: 1",
    "b.yml": "k: 2",
    "notes.json": '{"k": 3}',
    "sub/c.yaml": "k: 4",
    "sub/deep/d.yaml": "k: 5",
    "sub/data.json": '{"k": 6}',
}.items():
    p = root / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)

# ----------------------------------------------------------------------------
# 1. glob / rglob / iterdir -- sorted, deduplicated, pathlib-flavoured
# ----------------------------------------------------------------------------
top = pygim.path(root)
#                            ┌─ pattern: * and ? match within one component;
#                            │  `/` separates components; ** spans directories
#                            ▼
assert [f.name for f in top.glob("*.yaml")] == ["a.yaml"]
assert [f.name for f in top.glob("*.y*")] == ["a.yaml", "b.yml"]
assert [f.name for f in top.glob("sub/*.yaml")] == ["c.yaml"]
assert {f.name for f in top.rglob("*.yaml")} == {"a.yaml", "c.yaml", "d.yaml"}

children = top.iterdir()
assert [f.name for f in children] == sorted(f.name for f in children)

# ----------------------------------------------------------------------------
# 2. Traversal results inherit the engine pin
# ----------------------------------------------------------------------------
# Pin "yaml" on the root: every discovered file decodes as YAML -- even the
# .json ones (JSON is a YAML subset, so this is a legitimate uniform view).
pinned = pygim.path(root, engine="yaml")
hits = pinned.rglob("*.json")
assert all(f.engine == "yaml" for f in hits)
assert sorted(f.read()["k"] for f in hits) == [3, 6]

# ----------------------------------------------------------------------------
# 3. The PathSet bridge: set algebra over glob results
# ----------------------------------------------------------------------------
from pygim.pathset import PathSet

ps = top.pathset("**/*.yaml")
assert isinstance(ps, PathSet) and len(ps) == 3

# ----------------------------------------------------------------------------
# 4. Parallel reads: I/O + parsing overlap across threads
# ----------------------------------------------------------------------------
# The GIL is released while each file is read and parsed, so a thread pool
# genuinely overlaps that part of the work (object construction still
# serialises -- Python objects need the GIL).
files = []
for i in range(24):
    fp = root / f"bulk_{i:02d}.json"
    fp.write_text(json.dumps({"i": i, "rows": [{"n": j} for j in range(50)]}))
    files.append(pygim.path(fp))

with ThreadPoolExecutor(max_workers=8) as ex:
    results = list(ex.map(lambda f: f.read(), files))
assert all(results[i]["i"] == i and len(results[i]["rows"]) == 50 for i in range(24))

# ----------------------------------------------------------------------------
# 5. key_cache: interning repeated mapping keys
# ----------------------------------------------------------------------------
records = root / "records.json"
records.write_text(json.dumps([{"id": i, "name": "x"} for i in range(100)]))

#                                        ┌─ key_cache: max distinct interned
#                                        │  keys (0 = off, -1 = unbounded)
#                                        ▼
interned = pygim.path(records).read(key_cache=-1)
plain = pygim.path(records).read(key_cache=0)
assert interned == plain                              # capacity never changes meaning

k0, k1 = (next(iter(row)) for row in interned[:2])
assert k0 is k1                                       # same interned string object
p0, p1 = (next(iter(row)) for row in plain[:2])
assert p0 == p1                                       # equal but independently built

tmp.cleanup()
print("pathlike traversal & performance example OK:", top)
