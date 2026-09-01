# type: ignore
"""Walking directories: iterdir, glob, rglob -- and the PathSet bridge.

Traversal follows pathlib's vocabulary with two pygim twists: results are
sorted and deduplicated (deterministic across platforms), and every result
inherits the engine pin of the path that produced it.

This example demonstrates:
- glob patterns: * and ? within a component, / between components
- rglob for recursive matching (** under the hood)
- Engine-pin inheritance through traversal results
- pathset(): glob results as a pygim.pathset.PathSet
"""

import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)
for name, text in {
    "a.yaml": "k: 1",
    "b.yml": "k: 2",
    "sub/c.yaml": "k: 3",
    "sub/data.json": '{"k": 4}',
}.items():
    p = root / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)

top = pygim.path(root)

# ----------------------------------------------------------------------------
# 1. glob: one directory level per pattern component
# ----------------------------------------------------------------------------
#                            ┌─ pattern: * and ? match within one component;
#                            │  `/` descends into subdirectories
#                            ▼
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
pinned = pygim.path(root, engine="yaml")
hits = pinned.rglob("*.json")
assert all(f.engine == "rapidyaml" for f in hits)
assert hits[0].read() == {"k": 4}

# ----------------------------------------------------------------------------
# 4. The PathSet bridge: set algebra over glob results
# ----------------------------------------------------------------------------
from pygim.pathset import PathSet

ps = top.pathset("**/*.yaml")
assert isinstance(ps, PathSet) and len(ps) == 2

n_children = len(top.iterdir())
tmp.cleanup()
print("pathlike traversal example OK:", n_children, "children")
