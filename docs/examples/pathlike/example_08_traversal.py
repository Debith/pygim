# type: ignore
"""Walking directories: iterdir, glob, rglob -- and pathset(), for many paths.

Traversal follows pathlib's vocabulary with two pygim twists: results are
sorted and deduplicated (deterministic across platforms), and every result
inherits the engine pin of the path that produced it. A path is one file or
folder; many are a PathSet: pathset() walks as glob does and hands back the
set, keeping only files or folders (kind=) and passing folders over (skip=).

This example demonstrates:
- glob patterns: * and ? within a component, / between components
- rglob for recursive matching (** under the hood)
- Engine-pin inheritance through traversal results
- pathset(): glob results as a PathSet over the current store
- pathset(kind=..., skip=...): files or folders only, named folders never entered
"""

import os
import tempfile

import pygim

tmp = tempfile.TemporaryDirectory()
top = pygim.path(tmp.name)
for name, text in {
    "a.yaml": "k: 1",
    "b.yml": "k: 2",
    "sub/c.yaml": "k: 3",
    "sub/data.json": '{"k": 4}',
}.items():
    p = top / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)

# ----------------------------------------------------------------------------
# 1. glob: one directory level per pattern component
# ----------------------------------------------------------------------------
#                                ┌─ pattern: * and ? match within one component;
#                                │  `/` descends into subdirectories
#                                ▼
assert [f.name for f in top.glob("*.yaml")] == ["a.yaml"]
assert [f.name for f in top.glob("*.y*")] == ["a.yaml", "b.yml"]
assert [f.name for f in top.glob("sub/*.yaml")] == ["c.yaml"]

# ----------------------------------------------------------------------------
# 2. rglob: recursive, sorted, deduplicated
# ----------------------------------------------------------------------------
assert {f.name for f in top.rglob("*.yaml")} == {"a.yaml", "c.yaml"}
children = top.iterdir()
assert [f.name for f in children] == sorted(f.name for f in children)

# ----------------------------------------------------------------------------
# 3. Results inherit the engine pin of the path that produced them
# ----------------------------------------------------------------------------
# Pin "yaml" on the root and even .json hits decode as YAML (JSON is a
# YAML subset, so this gives a legitimate uniform view).
pinned = pygim.path(top, engine="yaml")
hits = pinned.rglob("*.json")
assert all(f.engine == "rapidyaml" for f in hits)
assert hits[0].read() == {"k": 4}

# ----------------------------------------------------------------------------
# 4. The PathSet bridge: set algebra over glob results
# ----------------------------------------------------------------------------
from pygim.pathlike import PathSet, ext

ps = top.pathset("**/*.yaml")
assert isinstance(ps, PathSet)
assert len(ps) == 2
assert top.glob("*.yaml")[0] in ps                  # a file is found by value
# the set is rows of top's table
assert ps[0] == pygim.path(os.fspath(ps[0]))
assert ps[0].store == top.store
assert len(ps & ext(".yaml")) == 2

# ----------------------------------------------------------------------------
# 5. Many paths at once: files or folders only, folders passed over
# ----------------------------------------------------------------------------
#                           ┌─ kind: "file" as is_file() says, "dir" as is_dir() says; the
#                           │  walk's listing tells, so no path is stat'ed for it
#                           │            ┌─ skip: folders never entered or matched (globs)
#                           ▼            ▼
files = top.pathset("**/*", kind="file", skip=["sub"])
assert [p.name for p in files] == ["a.yaml", "b.yml"]
assert [p.name for p in top.pathset("**/*", kind="dir")] == ["sub"]
assert files.sizes() == [len("k: 1"), len("k: 2")]   # each member's size, in member order

n_children = len(top.pathset())
tmp.cleanup()
print("pathlike traversal example OK:", n_children, "children")
