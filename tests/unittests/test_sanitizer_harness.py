# -*- coding: utf-8 -*-
"""tests/static/run_tests_under_sanitizers.py's mirror: OUT holds the working tree as it is now.

OUT is kept between runs, so its built modules stay valid; what the tree no longer has must leave it
all the same, or the tests run there against a file that is gone.
"""

import importlib.util

import pytest

import pygim

HARNESS = pygim.path(__file__).resolve().parent.parent / "static" / "run_tests_under_sanitizers.py"


@pytest.fixture(scope="module")
def harness():
    spec = importlib.util.spec_from_file_location("sanitizer_harness", HARNESS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def tree_and_out(harness, tmp_path, monkeypatch):
    """A tree of two files, mirrored once into OUT; the tree's files are what the test says they are."""
    tree, out = pygim.path(tmp_path) / "tree", pygim.path(tmp_path) / "out"
    (tree / "docs").mkdir(parents=True)
    for name in ("docs/old.py", "docs/kept.py"):
        (tree / name).write_text(name)
    monkeypatch.setattr(harness, "ROOT", tree)
    monkeypatch.setattr(harness, "tree_files", lambda: ["docs/old.py", "docs/kept.py"])
    harness.mirror(out)
    return tree, out


def files_under(folder):
    return sorted("/".join(p.parts) for p in folder.pathset("**/*", kind="file").relative_to(folder))


def test_a_file_gone_from_the_tree_is_gone_from_the_copy(harness, tree_and_out, monkeypatch):
    """Found 2026-11-01: an example renamed in the tree stayed in OUT under its old name, and the
    examples' index test failed there on a file the tree no longer had."""
    tree, out = tree_and_out
    (tree / "docs" / "new.py").write_text("docs/old.py")
    (tree / "docs" / "old.py").unlink()
    monkeypatch.setattr(harness, "tree_files", lambda: ["docs/new.py", "docs/kept.py"])
    harness.mirror(out)
    assert [n for n in files_under(out) if n.startswith("docs/")] == ["docs/kept.py", "docs/new.py"]


def test_what_the_build_made_in_the_copy_stays(harness, tree_and_out):
    """A module built in OUT is no tree file, and keeping it is what makes the next build quick."""
    tree, out = tree_and_out
    (out / "docs" / "built.so").write_bytes(b"\x7fELF")
    harness.mirror(out)
    assert (out / "docs" / "built.so").exists()
