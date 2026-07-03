# type: ignore
"""Working with file collections using ``pygim.pathset.PathSet``.

``PathSet`` is a C++-backed, set-like container of filesystem paths. Where
pathlib gives you one path at a time, PathSet treats a *collection* of paths
as the unit of work: build sets from any mix of strings and Path objects,
compare and subtract whole sets, and bulk-read file contents in one call.

This example demonstrates:
- Building PathSets from strings, Paths, and mixed lists
- Set semantics: length, truthiness, membership, equality, deduplication
- Removing paths with ``-=`` (by string or by another PathSet)
- Independent copies with ``clone()``
- Bulk-reading file contents with ``read_all_files()``
- Glob-style matching with ``match_pattern``
"""

import shutil
import tempfile
from pathlib import Path

from pygim.pathset import PathSet, match_pattern

# A scratch directory with a few real files keeps the example self-contained.
workdir = Path(tempfile.mkdtemp(prefix="pygim_pathset_example_"))
(workdir / "readme.txt").write_text("hello")
(workdir / "notes.txt").write_text("world")
(workdir / "logo.png").write_text("<binary>")

try:
    # ------------------------------------------------------------------------
    # 1. Building path sets
    # ------------------------------------------------------------------------
    # Constructors accept a single str or Path, or a list mixing both.
    # Iteration yields pathlib.Path objects, so PathSet plugs straight into
    # existing pathlib-based code.
    files = PathSet([
        workdir / "readme.txt",
        str(workdir / "notes.txt"),
        workdir / "logo.png",
    ])

    assert len(files) == 3
    assert bool(files) is True
    assert all(isinstance(p, Path) for p in files)

    # It is a *set*: duplicates collapse, and equal contents mean equal sets.
    assert PathSet([workdir / "readme.txt", workdir / "readme.txt"]) == PathSet(workdir / "readme.txt")
    assert not PathSet([])  # an empty set is falsy

    # Membership accepts strings or Path objects.
    assert str(workdir / "readme.txt") in files
    assert (workdir / "logo.png") in files

    # ------------------------------------------------------------------------
    # 2. Removing paths and cloning
    # ------------------------------------------------------------------------
    # clone() produces an independent copy: mutating the original afterwards
    # leaves the clone untouched.
    snapshot = files.clone()

    files -= str(workdir / "logo.png")  # remove one path by string
    assert len(files) == 2
    assert len(snapshot) == 3           # the clone kept the original contents

    # Subtracting a whole PathSet removes every path it contains.
    files -= PathSet(workdir / "notes.txt")
    assert len(files) == 1

    # ------------------------------------------------------------------------
    # 3. Bulk-reading file contents
    # ------------------------------------------------------------------------
    # read_all_files() returns the contents of every *regular file* in the
    # set -- one string per file; directories and missing paths are skipped.
    texts = PathSet([workdir / "readme.txt", workdir / "notes.txt"]).read_all_files()
    assert sorted(texts) == ["hello", "world"]

    # ------------------------------------------------------------------------
    # 4. Glob-style pattern matching
    # ------------------------------------------------------------------------
    # match_pattern implements the matching used for PathSet filtering.
    #                    ┌─ pattern: `*` = any run, `?` = one character
    #                    │        ┌─ the candidate string to test
    #                    ▼        ▼
    assert match_pattern("*.txt", "readme.txt")
    assert not match_pattern("*.txt", "logo.png")
    assert match_pattern("read??.txt", "readme.txt")

    print("PathSet example OK:", sorted(p.name for p in files))
finally:
    shutil.rmtree(workdir)
