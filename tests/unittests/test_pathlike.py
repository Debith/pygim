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


def test_json_engine_reads_natively(temp_dir):
    f = _write(temp_dir, "data.json", '{"a": 1}')
    assert pygim.path(f).read() == {"a": 1}           # simdjson engine, chosen by extension


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


# --------------------------------------------------------------------------- #
# Engine selection: read(engine=) > constructor pin > extension
# --------------------------------------------------------------------------- #
def test_engine_pinned_at_construction_reads_unknown_extension(temp_dir):
    f = _write(temp_dir, "config.dat", "name: test\ncount: 3\n")
    p = pygim.path(f, engine="yaml")
    assert p.engine == "yaml"
    assert p.read() == {"name": "test", "count": 3}


def test_engine_defaults_to_auto():
    assert pygim.path("a.yaml").engine is None            # auto: extension decides


def test_engine_pin_propagates_to_derived_paths():
    p = pygim.path("a/b.dat", engine="yaml")
    assert p.parent.engine == "yaml"
    assert p.with_name("c.dat").engine == "yaml"
    assert p.with_suffix(".cfg").engine == "yaml"
    assert (p / "sub.dat").engine == "yaml"
    assert all(x.engine == "yaml" for x in p.parents)


def test_engine_pin_shown_in_repr():
    assert repr(pygim.path("x.dat", engine="yaml")) == 'file("file://x.dat", engine=yaml)'
    assert repr(pygim.path("x.yaml")) == 'file("file://x.yaml")'   # auto: unchanged


def test_read_engine_overrides_construction_pin(temp_dir):
    f = _write(temp_dir, "doc.dat", '{"a": 1}')
    p = pygim.path(f, engine="yaml")
    assert p.read(engine="json") == {"a": 1}              # per-call override wins


def test_unknown_engine_name_raises_at_construction():
    with pytest.raises(ValueError, match="unknown engine: 'xml'"):
        pygim.path("a.yaml", engine="xml")


def test_unknown_extension_without_pin_still_refuses(temp_dir):
    f = _write(temp_dir, "mystery.dat", "k: v\n")
    with pytest.raises(ValueError, match="no engine for extension"):
        pygim.path(f).read()


# --------------------------------------------------------------------------- #
# JSON engine (simdjson): differential against the stdlib json module
# --------------------------------------------------------------------------- #
JSON_CORPUS = [
    '{"a": 1, "b": 2.5, "c": true, "d": false, "e": null}',
    '[1, [2, [3, [4]]], {"deep": {"deeper": []}}]',
    '{"unicode": "h\u00e4t\u00e4 \u2014 \u00e5\u00e4\u00f6", "empty": {}, "list": []}',
    '{"big": 9223372036854775807, "neg": -9223372036854775808, "u64": 18446744073709551615}',
    '{"floats": [0.1, 1e10, -2.5e-3, 123456.789]}',
    '"just a string"',
    '42',
    '[]',
]


@pytest.mark.parametrize("doc", JSON_CORPUS)
def test_json_read_matches_stdlib_json(temp_dir, doc):
    import json

    f = _write(temp_dir, "diff.json", doc)
    assert pygim.path(f).read() == json.loads(doc)


def test_json_engine_is_strict_not_yaml(temp_dir):
    # {a: 1} is valid YAML but invalid JSON: proves .json is NOT routed to the
    # YAML engine, and that the engine override still rescues the read.
    f = _write(temp_dir, "notjson.json", "{a: 1}")
    with pytest.raises(RuntimeError, match="JSON parse error"):
        pygim.path(f).read()
    assert pygim.path(f).read(engine="yaml") == {"a": 1}


# --------------------------------------------------------------------------- #
# YAML 1.2 core schema: hand-authored expected values (the spec, encoded)
# --------------------------------------------------------------------------- #
YAML_12_SCALARS = [
    # (document, expected)          — core-schema resolution, quoted stays str
    ("v: 0x1A", {"v": 26}),                    # hex int
    ("v: 0o17", {"v": 15}),                    # octal int
    ("v: 010", {"v": 10}),                     # leading zero is DECIMAL in 1.2
    ("v: 12345678901234567890123", {"v": 12345678901234567890123}),  # exact big int
    ("v: -12345678901234567890123", {"v": -12345678901234567890123}),
    ("v: yes", {"v": "yes"}),                  # 1.1 bools stay strings in 1.2
    ("v: On", {"v": "On"}),
    ("v: 1_000", {"v": "1_000"}),              # 1.1 underscore ints stay strings
    ("v: 0b1", {"v": "0b1"}),                  # binary is not core schema
    ("v: -0x1A", {"v": "-0x1A"}),              # signed hex is not core schema
    ("v: '0x1A'", {"v": "0x1A"}),              # quoting suppresses resolution
    ("v: 1e3", {"v": 1000.0}),                 # exponent float without dot
    ("v: .5", {"v": 0.5}),
    ("v: 1e999", {"v": float("inf")}),         # overflow saturates like float()
    ("v: inf", {"v": "inf"}),                  # strtod-isms are NOT core floats
    ("v: nan", {"v": "nan"}),
    ("v: Infinity", {"v": "Infinity"}),
]


@pytest.mark.parametrize("doc,expected", YAML_12_SCALARS)
def test_yaml_12_core_schema_scalars(temp_dir, doc, expected):
    f = _write(temp_dir, "scalar.yaml", doc + "\n")
    obj = pygim.path(f).read()
    assert obj == expected
    for k in expected:
        assert type(obj[k]) is type(expected[k])


# --------------------------------------------------------------------------- #
# Differential harness: cross-check against PyYAML on 1.1/1.2 common ground
# --------------------------------------------------------------------------- #
YAML_COMMON_GROUND = [
    "a: 1\nb: 2.5\nc: true\nd: null\ne: text\n",
    "list:\n  - 1\n  - two\n  - 3.0\n  - null\n",
    "nested:\n  x:\n    y:\n      z: [1, 2, 3]\n",
    "empty_map: {}\nempty_list: []\n",
    'quoted: "123"\nsingle: \'true\'\n',
    "anchors:\n  base: &b {k: 1}\n  copy: *b\n",
    "negative: -17\nzero: 0\nplus: +5\n",
    "inf: .inf\nneg_inf: -.inf\n",
    "multi: |\n  line one\n  line two\n",
]


@pytest.mark.parametrize("doc", YAML_COMMON_GROUND)
def test_yaml_read_matches_pyyaml_on_common_ground(temp_dir, doc):
    yaml = pytest.importorskip("yaml")

    f = _write(temp_dir, "common.yaml", doc)
    assert pygim.path(f).read() == yaml.safe_load(doc)
