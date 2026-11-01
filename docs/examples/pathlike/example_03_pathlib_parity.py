# type: ignore
"""A pygim path is an os.PathLike with pathlib-style components.

Reading is the headline feature, but ``file`` is a full citizen of the
path ecosystem: it drops into ``open()``, ``os.fspath()``, ``pathlib.Path``,
and offers the familiar component accessors and ``/`` composition.

This example demonstrates:
- os.PathLike integration (open, fspath, pathlib)
- Name components: name / stem / suffix / suffixes / parts
- Composition with ``/`` and parent navigation
- A value, as pathlib's: it sorts, pickles and copies
"""

import copy
import os
import pathlib   # what this example compares a pygim path with (sections 1 and 4)
import pickle
import tempfile

import pygim

tmp = tempfile.TemporaryDirectory()
p = pygim.path(tmp.name) / "app.yaml"
p.write_text("k: 1\n")

# ----------------------------------------------------------------------------
# 1. It IS a path: open(), os.fspath(), pathlib all accept it
# ----------------------------------------------------------------------------
assert os.fspath(p) == os.path.join(tmp.name, "app.yaml")
assert pathlib.Path(p).read_text() == "k: 1\n"
with open(p) as fh:
    assert fh.read() == "k: 1\n"

# ----------------------------------------------------------------------------
# 2. pathlib-style components
# ----------------------------------------------------------------------------
assert p.name == "app.yaml"
assert p.stem == "app"
assert p.suffix == ".yaml"
assert pygim.path("archive.tar.gz").suffixes == [".tar", ".gz"]

# ----------------------------------------------------------------------------
# 3. Composition: `/` builds new paths; parent walks up
# ----------------------------------------------------------------------------
again = p.parent / "app.yaml"
assert again == p
assert again.read() == {"k": 1}

# ----------------------------------------------------------------------------
# 4. A value, as pathlib's is: it sorts, pickles and copies
# ----------------------------------------------------------------------------
names = ["b", "a/b", "a-b", "a"]
ours = sorted(pygim.path(n) for n in names)
theirs = sorted(pathlib.PurePath(n) for n in names)
assert [str(x) for x in ours] == [str(x) for x in theirs]
assert pickle.loads(pickle.dumps(p)) == p   # so it reaches another process
assert copy.copy(p) is p                    # a value: the object itself

tmp.cleanup()
print("pathlike pathlib-parity example OK:", p.name)
