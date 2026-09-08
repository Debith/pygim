# type: ignore
"""Working with file collections: ``pygim.PathSet``, filters and queries.

Where pathlib gives you one path at a time, ``PathSet`` treats a *collection*
of paths as the unit of work — and stores it as ONE table: every distinct
component once, every path a row, so a set of a million paths costs about
100 bytes each. Build sets from any mix of strings, Path objects and pygim
files; compare, add and subtract whole sets; filter with a small algebra of
predicates; bulk-read file contents in one call.

This example demonstrates:
- Building PathSets from one path or an iterable of mixed kinds
- Set semantics: length, truthiness, membership, equality, deduplication
- Subtracting paths with ``-`` (a path, several, or another PathSet) — sets
  are values: ``files -= x`` rebinds the name, it never edits a set in place
- ``clone()`` for a distinct object with the same members
- Filters (``ext``, ``name``, ``absolute``; ``&``, ``|``, ``~``) and lazy
  ``Query`` objects: ``ps & f`` evaluates on demand
- Bulk-reading file contents with ``read_all_files()``
- Glob-style matching with ``match_pattern``
- Counting a union or intersection without building it
"""

import os
import shutil
import tempfile
from pathlib import Path

import pygim
from pygim.pathlike import PathSet, ext, match_pattern, name

# A scratch directory with a few real files keeps the example self-contained.
workdir = Path(tempfile.mkdtemp(prefix="pygim_pathset_example_"))
(workdir / "readme.txt").write_text("hello")
(workdir / "notes.txt").write_text("world")
(workdir / "logo.png").write_text("<binary>")

try:
    # ------------------------------------------------------------------------
    # 1. Building path sets
    # ------------------------------------------------------------------------
    # One path, or an iterable mixing str, pathlib.Path and pygim files.
    # Iteration yields path objects over the set's table (name, suffix, parent,
    # ==, hash all read by row; typed by extension like any path).
    files = PathSet([
        workdir / "readme.txt",
        str(workdir / "notes.txt"),
        pygim.path(workdir / "logo.png"),
    ])

    assert len(files) == 3
    assert bool(files) is True
    assert all(isinstance(os.fspath(v), str) and v.name for v in files)

    # It is a *set*: duplicates collapse, and equal members mean equal sets.
    assert PathSet([workdir / "readme.txt", workdir / "readme.txt"]) == PathSet(workdir / "readme.txt")
    assert not PathSet([])  # an empty set is falsy

    # Membership accepts strings, Path objects, files and views.
    assert str(workdir / "readme.txt") in files
    assert (workdir / "logo.png") in files
    assert pygim.path(workdir / "notes.txt") in files

    # ------------------------------------------------------------------------
    # 2. Subtracting and cloning
    # ------------------------------------------------------------------------
    # A set is a value. `files -= x` computes a new set and rebinds the name,
    # so anything else holding the old set keeps seeing the old members.
    snapshot = files.clone()          # a distinct object, the same members
    assert snapshot == files and snapshot is not files

    files -= str(workdir / "logo.png")  # remove one path by string
    assert len(files) == 2
    assert len(snapshot) == 3           # the snapshot kept the original members

    # Subtracting a whole PathSet removes every path it contains.
    files -= PathSet(workdir / "notes.txt")
    assert len(files) == 1

    # And + is the union, taking a PathSet or path(s).
    assert len(files + snapshot) == 3 and len(files + [workdir / "logo.png"]) == 2

    # ------------------------------------------------------------------------
    # 3. Filters and lazy queries
    # ------------------------------------------------------------------------
    # A Filter is a predicate over the table (never the filesystem); & | ~
    # combine them. `ps & f` is a Query — nothing runs until eval(), len()
    # or iteration.
    txt = snapshot & ext(".txt")                 # a Query
    assert len(txt) == 2 and all(v.suffix == ".txt" for v in txt)
    assert (snapshot & ~ext(".txt")).eval().to_list() == [str(workdir / "logo.png")]
    assert len(snapshot & (ext(".txt") | ext(".png"))) == 3
    assert len(snapshot & name("read*")) == 1
    assert snapshot.filter(ext(".png")) == PathSet(workdir / "logo.png")

    # Only the size of a set operation? No result set is built.
    assert snapshot.count_intersection(files) == 1

    # ------------------------------------------------------------------------
    # 4. Bulk-reading file contents
    # ------------------------------------------------------------------------
    # read_all_files() returns the contents of every *regular file* in the
    # set -- one string per file; directories and missing paths are skipped.
    texts = PathSet([workdir / "readme.txt", workdir / "notes.txt"]).read_all_files()
    assert sorted(texts) == ["hello", "world"]

    # ------------------------------------------------------------------------
    # 5. Glob-style pattern matching
    # ------------------------------------------------------------------------
    # match_pattern is the matcher behind name() and filter_name().
    #                    ┌─ pattern: `*` = any run, `?` = one character
    #                    │        ┌─ the candidate string to test
    #                    ▼        ▼
    assert match_pattern("*.txt", "readme.txt")
    assert not match_pattern("*.txt", "logo.png")
    assert match_pattern("read??.txt", "readme.txt")

    print("PathSet example OK:", sorted(v.name for v in files))
finally:
    shutil.rmtree(workdir)
