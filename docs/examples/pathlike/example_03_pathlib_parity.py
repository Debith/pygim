# type: ignore
"""A pygim path is an os.PathLike with pathlib-style components.

Reading is the headline feature, but ``file`` is a full citizen of the
path ecosystem: it drops into ``open()``, ``os.fspath()``, ``pathlib.Path``,
and offers the familiar component accessors and ``/`` composition.

This example demonstrates:
- os.PathLike integration (open, fspath, pathlib)
- Name components: name / stem / suffix / suffixes / parts
- Composition with ``/`` and parent navigation
"""

import os
import pathlib
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
config = Path(tmp.name) / "app.yaml"
config.write_text("k: 1\n")

p = pygim.path(config)

# ----------------------------------------------------------------------------
# 1. It IS a path: open(), os.fspath(), pathlib all accept it
# ----------------------------------------------------------------------------
assert os.fspath(p) == str(config)
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

tmp.cleanup()
print("pathlike pathlib-parity example OK:", p.name)
