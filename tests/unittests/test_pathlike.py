# -*- coding: utf-8 -*-
"""Tests for ``pygim.path`` — the self-reading, self-decoding PathLike."""

import os

import pytest

import pygim
from pygim import pathlike
import pathlib


def _write(temp_dir, name, text):
    p = temp_dir / name
    p.write_text(text)
    return p


# --------------------------------------------------------------------------- #
# PathLike identity
# --------------------------------------------------------------------------- #
def test_path_returns_a_pathlike_file():
    p = pygim.path("some.yaml")
    assert isinstance(p, pathlike.file)
    assert isinstance(p, os.PathLike)
    assert os.fspath(p) == "some.yaml"
    assert repr(p) == 'file("file://some.yaml")'
    assert p.suffix == ".yaml"
    assert p.uri == "file://some.yaml"


def test_file_is_a_registered_pathlike_subclass():
    # Explicitly registered with the ABC, not merely duck-typed.
    assert issubclass(pathlike.file, os.PathLike)


def test_file_integrates_with_pathlib_and_open(temp_dir):
    import pathlib

    f = _write(temp_dir, "conf.yaml", "k: v\n")
    assert pathlib.Path(pygim.path(f)).read_text() == "k: v\n"   # PathLike -> Path
    with open(pygim.path(f)) as fh:
        assert fh.read() == "k: v\n"


def test_file_is_usable_with_open(temp_dir):
    f = _write(temp_dir, "hello.yaml", "x: 1\n")
    with open(pygim.path(f)) as fh:          # PathLike drops straight into open()
        assert fh.read() == "x: 1\n"


# --------------------------------------------------------------------------- #
# YAML decoding + scalar type inference
# --------------------------------------------------------------------------- #
def test_read_infers_scalar_types(temp_dir):
    f = _write(temp_dir, "doc.yaml", "level: 12\nratio: 2.5\nalive: true\nnotes: null\nname: Ed\n")
    obj = pygim.path(f).read()
    assert obj == {"level": 12, "ratio": 2.5, "alive": True, "notes": None, "name": "Ed"}
    assert isinstance(obj["level"], int)
    assert isinstance(obj["ratio"], float)
    assert isinstance(obj["alive"], bool)
    assert obj["notes"] is None


def test_read_handles_nesting(temp_dir):
    f = _write(temp_dir, "nested.yaml", "a:\n  b:\n    - 1\n    - two\n    - 3.0\n")
    assert pygim.path(f).read() == {"a": {"b": [1, "two", 3.0]}}


def test_quoted_scalar_stays_a_string(temp_dir):
    f = _write(temp_dir, "q.yaml", 'id: "123"\nk: 123\n')
    obj = pygim.path(f).read()
    assert obj["id"] == "123" and isinstance(obj["id"], str)   # quoted -> str
    assert obj["k"] == 123 and isinstance(obj["k"], int)        # bare -> int


def test_anchors_and_aliases_are_resolved(temp_dir):
    f = _write(temp_dir, "anchor.yaml", "base: &b {hp: 10}\nref: *b\n")
    assert pygim.path(f).read() == {"base": {"hp": 10}, "ref": {"hp": 10}}


def test_empty_document_reads_as_none(temp_dir):
    f = _write(temp_dir, "empty.yaml", "")
    assert pygim.path(f).read() is None


def test_special_floats(temp_dir):
    f = _write(temp_dir, "floats.yaml", "big: .inf\nsmall: -.inf\n")
    obj = pygim.path(f).read()
    assert obj["big"] == float("inf") and obj["small"] == float("-inf")


# --------------------------------------------------------------------------- #
# bytes + engine selection
# --------------------------------------------------------------------------- #
def test_read_bytes_is_raw(temp_dir):
    f = temp_dir / "raw.yaml"
    f.write_bytes(b"x: 1\n")
    assert pygim.path(f).read_bytes() == b"x: 1\n"


def test_engine_override_when_extension_is_absent(temp_dir):
    f = _write(temp_dir, "plain", "k: v\n")          # no extension -> needs override
    assert pygim.path(f).read(engine="yaml") == {"k": "v"}


def test_unknown_extension_raises(temp_dir):
    f = _write(temp_dir, "data.weird", "a: 1\n")
    with pytest.raises(ValueError):
        pygim.path(f).read()


def test_unknown_engine_raises(temp_dir):
    f = _write(temp_dir, "data.yaml", "a: 1\n")
    with pytest.raises(ValueError):
        pygim.path(f).read(engine="toml")


def test_json_engine_not_implemented_yet(temp_dir):
    f = _write(temp_dir, "data.json", '{"a": 1}')
    with pytest.raises(RuntimeError):
        pygim.path(f).read()                          # reserved for a future simdjson engine


def test_missing_file_raises(temp_dir):
    with pytest.raises((RuntimeError, OSError)):
        pygim.path(temp_dir / "nope.yaml").read()


def test_malformed_yaml_raises_not_aborts(temp_dir):
    # An undefined alias is a resolve error; the throwing callback must turn it into
    # a Python exception rather than aborting the process.
    f = _write(temp_dir, "bad.yaml", "ref: *missing\n")
    with pytest.raises(RuntimeError):
        pygim.path(f).read()


# --------------------------------------------------------------------------- #
# path composition (pathlib-style)
# --------------------------------------------------------------------------- #
def test_truediv_joins_and_returns_a_file():
    p = pygim.path("base") / "sub" / "doc.yaml"
    assert isinstance(p, pathlike.file)
    assert os.fspath(p) == "base/sub/doc.yaml"
    assert p.suffix == ".yaml"                       # still a decodable file after joining


def test_rtruediv_joins_from_a_string():
    p = "root" / pygim.path("leaf.yaml")
    assert os.fspath(p) == "root/leaf.yaml"


def test_joinpath_appends_many_components():
    assert os.fspath(pygim.path("a").joinpath("b", "c.yaml")) == "a/b/c.yaml"


def test_absolute_component_replaces():
    # std::filesystem / pathlib semantics: an absolute right-hand side wins.
    assert os.fspath(pygim.path("a/b") / "/etc/x.yaml") == "/etc/x.yaml"


def test_parent_name_stem_and_flags():
    p = pygim.path("a/b/c.yaml")
    assert os.fspath(p.parent) == "a/b" and isinstance(p.parent, pathlike.file)
    assert p.name == "c.yaml" and p.stem == "c"
    assert p.is_absolute() is False
    assert pygim.path("/x/y").is_absolute() is True


def test_join_then_read(temp_dir):
    (temp_dir / "sub").mkdir()
    _write(temp_dir, "sub/data.yaml", "k: v\n")
    assert (pygim.path(temp_dir) / "sub" / "data.yaml").read() == {"k": "v"}
    assert (pygim.path(temp_dir) / "sub" / "data.yaml").exists()


# --------------------------------------------------------------------------- #
# name components + derived paths (pathlib parity)
# --------------------------------------------------------------------------- #
def test_suffix_preserves_case_but_engine_is_case_insensitive(temp_dir):
    assert pygim.path("DATA.YAML").suffix == ".YAML"       # case preserved, unlike engine lookup
    f = _write(temp_dir, "UP.YAML", "k: v\n")
    assert pygim.path(f).read() == {"k": "v"}              # still resolves the yaml engine


def test_suffixes_and_parts():
    p = pygim.path("data/archive.tar.gz")
    assert p.suffix == ".gz"
    assert p.suffixes == [".tar", ".gz"]
    assert p.stem == "archive.tar"
    assert p.parts == ["data", "archive.tar.gz"]
    assert pygim.path(".bashrc").suffixes == []           # leading-dot name has no suffixes


def test_parents_closest_first():
    assert [os.fspath(x) for x in pygim.path("a/b/c.yaml").parents] == ["a/b", "a"]


def test_with_suffix_name_stem():
    assert os.fspath(pygim.path("a/b.yaml").with_suffix(".json")) == "a/b.json"
    assert os.fspath(pygim.path("a/b.yaml").with_name("c.txt")) == "a/c.txt"
    assert os.fspath(pygim.path("a/b.yaml").with_stem("z")) == "a/z.yaml"


def test_resolve_collapses_dotdot():
    assert pygim.path("a/../b/c.yaml").resolve().name == "c.yaml"


def test_status_and_size(temp_dir):
    f = _write(temp_dir, "sized.yaml", "k: v\n")
    p = pygim.path(f)
    assert p.exists() and p.is_file() and not p.is_dir()
    assert p.size() == 5
    assert pygim.path(temp_dir).is_dir()


def test_file_is_hashable():
    assert len({pygim.path("a"), pygim.path("a"), pygim.path("b")}) == 2
