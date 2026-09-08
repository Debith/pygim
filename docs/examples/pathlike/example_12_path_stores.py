# type: ignore
"""Choosing where paths live: ``PathStore`` and the ``store=`` argument.

Every ``pygim.path`` is a handle on a row of a table: the table stores each
distinct path component once and each distinct path as one row. ``path(text)``
puts the row in the module's default table; ``path(text, store=s)`` puts it in
``s``'s — and every path derived from that one (parent, ``/``, glob results)
stays in the same table. So a program chooses a lifetime once, by argument:
make a store, pass it, keep it while the paths matter, drop it.

This example demonstrates:
- The default store and a private one
- Derived paths inheriting their table
- Equality and hashing by value across stores (no object identity is promised)
- A PathSet over a store's table meeting its paths without re-interning
- What a store reports about itself, and that rows are never freed within it
"""

import gc
import tempfile
from pathlib import Path

import pygim
from pygim.pathlike import PathSet, PathStore, default_store, yamlpath

# ----------------------------------------------------------------------------
# 1. The default store, and a private one
# ----------------------------------------------------------------------------
p = pygim.path("etc/app/config.yaml")
assert p.store == default_store()               # no store= : the module's table
assert isinstance(p, yamlpath)                  # typed by extension, as always

#              ┌─ a table of its own: nothing shared with the default
#              ▼
mine = PathStore()
q = pygim.path("etc/app/config.yaml", store=mine)
assert q.store == mine and q.store != p.store

# ----------------------------------------------------------------------------
# 2. Derived paths inherit the table of the path they came from
# ----------------------------------------------------------------------------
assert q.parent.store == mine
assert (q.parent / "secrets.toml").store == mine
assert q.with_suffix(".json").store == mine
rows_before = mine.stats()["rows"]
_ = q.parent / "secrets.toml"                    # the same row again: nothing added
assert mine.stats()["rows"] == rows_before + 0

# ----------------------------------------------------------------------------
# 3. Equal by value across stores; identity is not promised
# ----------------------------------------------------------------------------
assert p == q and hash(p) == hash(q)             # the same path, two tables
assert pygim.path("etc//app/./config.yaml") == p # spellings collapse to one row
assert len({p, q, pygim.path("etc/app/config.yaml")}) == 1
# two handles on one row are equal — whether they are the same object is
# not part of the contract, as with CPython's interned strings

# ----------------------------------------------------------------------------
# 4. A PathSet over a store's table: its members are rows of that table
# ----------------------------------------------------------------------------
ps = PathSet(["etc/app/config.yaml", "etc/app/secrets.toml", "var/log"], store=mine)
assert q in ps                                   # a bit test: same table, same row
assert ps[0] == q and ps[0].store == mine
assert ps.stats()["rows"] == mine.stats()["rows"]

# ----------------------------------------------------------------------------
# 5. A lifetime chosen by argument
# ----------------------------------------------------------------------------
# A table lives as long as any handle or set over it. Rows are never freed
# within a table, so a long-running program gives each unit of work its own
# store and drops it — the default table never sees the traffic.
with tempfile.TemporaryDirectory() as tmp:
    for name in ("a.yaml", "b.yaml"):
        (Path(tmp) / name).write_text("k: 1")
    job = PathStore()
    root = pygim.path(tmp, store=job)
    hits = root.glob("*.yaml")                   # rows of `job`, typed, pin inherited
    assert all(h.store == job for h in hits) and len(hits) == 2
    assert default_store().stats()["rows"] == default_store().stats()["rows"]  # untouched by the walk
    stats = job.stats()
    assert set(stats) == {"rows", "segments", "bytes"} and stats["bytes"] > 0
    del job, root, hits                          # the last handles go: the table with them
    gc.collect()

print("PathStore example OK:", mine)
