# type: ignore
"""The performance knobs: parallel reads and key interning.

Two properties matter when decoding many or large documents:

- ``read()`` releases the GIL during file I/O and parsing, so reads from a
  thread pool overlap that part of the work.
- ``read(key_cache=N)`` interns repeated mapping keys: arrays of records
  reuse ONE Python string per distinct key instead of rebuilding it per row.

This example demonstrates:
- Correct concurrent reads through a ThreadPoolExecutor
- key_cache semantics: capacity is an optimisation, never a meaning change
- Proof of interning: identical key objects, not just equal ones
"""

import json
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

# ----------------------------------------------------------------------------
# 1. Parallel reads: I/O + parsing overlap across threads
# ----------------------------------------------------------------------------
files = []
for i in range(16):
    fp = root / f"bulk_{i:02d}.json"
    fp.write_text(json.dumps({"i": i, "rows": [{"n": j} for j in range(50)]}))
    files.append(pygim.path(fp))

with ThreadPoolExecutor(max_workers=8) as ex:
    results = list(ex.map(lambda f: f.read(), files))
assert all(results[i]["i"] == i for i in range(16))

# ----------------------------------------------------------------------------
# 2. key_cache: capacity never changes what you get back
# ----------------------------------------------------------------------------
records = root / "records.json"
records.write_text(json.dumps([{"id": i, "name": "x"} for i in range(100)]))

#                                        ┌─ key_cache: max distinct interned
#                                        │  keys (0 = off, -1 = unbounded)
#                                        ▼
interned = pygim.path(records).read(key_cache=-1)
plain = pygim.path(records).read(key_cache=0)
assert interned == plain

# ----------------------------------------------------------------------------
# 3. Proof of interning: the SAME string object across rows
# ----------------------------------------------------------------------------
k0, k1 = (next(iter(row)) for row in interned[:2])
assert k0 is k1                     # one interned py-string, reused
p0, p1 = (next(iter(row)) for row in plain[:2])
assert p0 == p1                     # equal, but independently built

tmp.cleanup()
print("pathlike parallel & key-cache example OK:", len(results), "files")
