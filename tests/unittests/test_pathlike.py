# -*- coding: utf-8 -*-
"""Tests for ``pygim.path`` — the self-reading, self-decoding PathLike."""

import os

import pytest

import pygim
from pygim import pathlike
import pathlib


def _write(temp_dir, name, text):
    p = temp_dir / name
    # Explicit encoding and newline: Windows would otherwise write cp1252 and
    # CRLF, breaking the UTF-8 parsers and byte-size assertions.
    p.write_text(text, encoding="utf-8", newline="\n")
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
        pygim.path(f).read(engine="xml")


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
    assert pathlib.PurePath(os.fspath(p)) == pathlib.PurePath("base/sub/doc.yaml")
    assert p.suffix == ".yaml"                       # still a decodable file after joining


def test_rtruediv_joins_from_a_string():
    p = "root" / pygim.path("leaf.yaml")
    assert pathlib.PurePath(os.fspath(p)) == pathlib.PurePath("root/leaf.yaml")


def test_joinpath_appends_many_components():
    joined = pygim.path("a").joinpath("b", "c.yaml")
    assert pathlib.PurePath(os.fspath(joined)) == pathlib.PurePath("a/b/c.yaml")


def test_absolute_component_replaces():
    # std::filesystem / pathlib semantics: an absolute right-hand side wins.
    assert os.fspath(pygim.path("a/b") / "/etc/x.yaml") == "/etc/x.yaml"


def test_parent_name_stem_and_flags():
    p = pygim.path("a/b/c.yaml")
    assert pathlib.PurePath(os.fspath(p.parent)) == pathlib.PurePath("a/b")
    assert isinstance(p.parent, pathlike.file)
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


# --------------------------------------------------------------------------- #
# TOML engine (toml++): differential against the stdlib tomllib
# --------------------------------------------------------------------------- #
TOML_CORPUS = [
    'title = "basic"\ncount = 42\nratio = 2.5\nactive = true\n',
    '[server]\nhost = "x"\nports = [8001, 8002]\n[server.limits]\nmax = 10\n',
    'day = 2024-01-15\ntea = 10:30:15\nstamp = 2024-01-15T10:30:00+02:00\nlocal = 2024-01-15T10:30:00\n',
    '[[points]]\nx = 1\n[[points]]\nx = 2\n',
    'inline = { a = 1, b = "two" }\nunicode = "hätä"\nneg = -17\nbig_f = 1e10\n',
]


@pytest.mark.parametrize("doc", TOML_CORPUS)
def test_toml_read_matches_tomllib(temp_dir, doc):
    import tomllib

    f = _write(temp_dir, "diff.toml", doc)
    assert pygim.path(f).read() == tomllib.loads(doc)


def test_toml_write_not_implemented(temp_dir):
    with pytest.raises(ValueError, match="toml write not implemented"):
        pygim.path(temp_dir / "out.toml").write({"a": 1})


def test_toml_parse_error_has_filename_and_line(temp_dir):
    f = _write(temp_dir, "broken.toml", "a = \n")
    with pytest.raises(RuntimeError, match="TOML parse error.*broken.toml.*line"):
        pygim.path(f).read()


# --------------------------------------------------------------------------- #
# write(): serialisation round-trips through the same classifiers as read()
# --------------------------------------------------------------------------- #
WRITE_ROUNDTRIP_OBJS = [
    {"name": "plain", "count": 3, "ratio": 2.5, "on": True, "off": False, "none": None},
    {"tricky_strings": ["true", "null", "0x1A", "1e3", "yes", ".inf", "", "010"]},
    {"big": 12345678901234567890123, "neg": -12345678901234567890123},
    {"specials": [float("inf"), float("-inf")]},
    {"nested": {"a": [1, {"b": [2, 3]}, "x"], "empty_list": [], "empty_map": {}}},
    {"unicode": "hätä — åäö", "key with space": 1},
    [1, "two", None, {"k": "v"}],
]


@pytest.mark.parametrize("obj", WRITE_ROUNDTRIP_OBJS)
def test_yaml_write_read_roundtrip(temp_dir, obj):
    p = pygim.path(temp_dir / "rt.yaml")
    p.write(obj)
    back = p.read()
    assert back == obj
    # types too, not just equality (True == 1 would slip through ==)
    assert repr(back) == repr(obj)


def test_yaml_write_nan_roundtrip(temp_dir):
    import math

    p = pygim.path(temp_dir / "nan.yaml")
    p.write({"v": float("nan")})
    assert math.isnan(p.read()["v"])


@pytest.mark.parametrize("obj", [o for o in WRITE_ROUNDTRIP_OBJS
                                 if not (isinstance(o, dict) and ("specials" in o or "big" in o))])
def test_json_write_matches_stdlib(temp_dir, obj):
    import json

    p = pygim.path(temp_dir / "rt.json")
    p.write(obj)
    assert json.loads(open(p, encoding="utf-8").read()) == obj
    assert p.read() == obj


def test_json_write_rejects_non_finite(temp_dir):
    with pytest.raises(ValueError, match="non-finite"):
        pygim.path(temp_dir / "x.json").write({"v": float("inf")})


def test_write_rejects_non_str_keys_and_unknown_types(temp_dir):
    p = pygim.path(temp_dir / "x.yaml")
    with pytest.raises(ValueError, match="keys must be str"):
        p.write({1: "a"})
    with pytest.raises(ValueError, match="unsupported type"):
        p.write({"a": {1, 2}})


def test_write_resolves_engine_like_read(temp_dir):
    p = pygim.path(temp_dir / "data.dat", engine="json")   # pin drives write too
    p.write({"a": 1})
    import json

    assert json.loads(open(p).read()) == {"a": 1}
    with pytest.raises(ValueError, match="no engine"):
        pygim.path(temp_dir / "x.dat").write({"a": 1})     # nothing resolves


# --------------------------------------------------------------------------- #
# Parallel reads: I/O + parsing run with the GIL released
# --------------------------------------------------------------------------- #
def test_parallel_reads_are_correct(temp_dir):
    from concurrent.futures import ThreadPoolExecutor

    files = []
    for i in range(48):
        ext = [".yaml", ".json", ".toml"][i % 3]
        text = {
            ".yaml": f"v: {i}\nitems: [1, 2]\n",
            ".json": f'{{"v": {i}, "items": [1, 2]}}',
            ".toml": f"v = {i}\nitems = [1, 2]\n",
        }[ext]
        files.append(pygim.path(_write(temp_dir, f"p{i}{ext}", text)))

    with ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(lambda f: f.read(), files))
    assert all(results[i] == {"v": i, "items": [1, 2]} for i in range(48))


def test_parallel_reads_of_same_file(temp_dir):
    from concurrent.futures import ThreadPoolExecutor

    f = pygim.path(_write(temp_dir, "shared.yaml", "k: [1, 2, 3]\n"))
    with ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(lambda _: f.read(), range(64)))
    assert all(r == {"k": [1, 2, 3]} for r in results)


# --------------------------------------------------------------------------- #
# Key-interning cache: capacity is a pure optimisation, never a semantic
# --------------------------------------------------------------------------- #
@pytest.mark.parametrize("size", [0, 2, 256, -1])
def test_key_cache_sizes_are_equivalent(temp_dir, size):
    import json

    rows = [{"id": i, "name": f"n{i % 3}", "value": i} for i in range(50)]
    f = _write(temp_dir, "rows.json", json.dumps(rows))
    assert pygim.path(f).read(key_cache=size) == rows


def test_key_cache_interns_identical_key_objects(temp_dir):
    f = _write(temp_dir, "two.json", '[{"key": 1}, {"key": 2}]')
    a, b = pygim.path(f).read(key_cache=-1)
    assert next(iter(a)) is next(iter(b))          # same interned py::str
    a0, b0 = pygim.path(f).read(key_cache=0)
    assert next(iter(a0)) == next(iter(b0))        # equal, not required to be identical


# --------------------------------------------------------------------------- #
# Directory traversal: iterdir / glob / rglob / pathset bridge
# --------------------------------------------------------------------------- #
@pytest.fixture()
def tree(temp_dir):
    for name in ["a.yaml", "b.yml", "c.json", "sub/d.yaml", "sub/deep/e.yaml", "sub/f.json"]:
        p = temp_dir / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("k: 1\n" if not name.endswith(".json") else '{"k": 1}')
    return temp_dir


def test_glob_single_level(tree):
    assert [f.name for f in pygim.path(tree).glob("*.yaml")] == ["a.yaml"]
    assert [f.name for f in pygim.path(tree).glob("*.y*")] == ["a.yaml", "b.yml"]
    assert [f.name for f in pygim.path(tree).glob("sub/*.yaml")] == ["d.yaml"]


def test_rglob_recursive_and_sorted(tree):
    names = [f.name for f in pygim.path(tree).rglob("*.yaml")]
    assert names == sorted(names) and set(names) == {"a.yaml", "d.yaml", "e.yaml"}


def test_glob_results_inherit_engine_pin(tree):
    hits = pygim.path(tree, engine="yaml").rglob("*.json")
    assert hits and all(f.engine == "yaml" for f in hits)
    assert hits[0].read() == {"k": 1}              # pin overrides .json extension


def test_iterdir_sorted_children(tree):
    names = [f.name for f in pygim.path(tree).iterdir()]
    assert names == sorted(names) and "sub" in names


def test_glob_rejects_bad_patterns(tree):
    with pytest.raises(ValueError):
        pygim.path(tree).glob("")
    with pytest.raises(ValueError):
        pygim.path(tree).glob("/abs/*.yaml")


def test_glob_on_missing_directory_is_empty(temp_dir):
    assert pygim.path(temp_dir / "nope").glob("*.yaml") == []


def test_pathset_bridge(tree):
    from pygim.pathset import PathSet

    ps = pygim.path(tree).pathset("**/*.yaml")
    assert isinstance(ps, PathSet) and len(ps) == 3


# --------------------------------------------------------------------------- #
# Pinned YAML semantics discovered empirically (see also the 1.2 corpus)
# --------------------------------------------------------------------------- #
def test_duplicate_mapping_keys_last_wins(temp_dir):
    # YAML calls duplicate keys invalid; like PyYAML and json.loads we take
    # the last occurrence rather than erroring. Pinned deliberately.
    f = _write(temp_dir, "dup.yaml", "a: 1\na: 2\n")
    assert pygim.path(f).read() == {"a": 2}


def test_merge_keys_are_expanded(temp_dir):
    # rapidyaml's resolve() expands YAML 1.1 merge keys; we inherit (and want)
    # that behaviour even though 1.2 dropped `<<` from the spec.
    f = _write(temp_dir, "mk.yaml", "base: &b\n  x: 1\nderived:\n  <<: *b\n  y: 2\n")
    assert pygim.path(f).read()["derived"] == {"x": 1, "y": 2}


def test_yaml_parse_error_includes_filename(temp_dir):
    f = _write(temp_dir, "broken.yaml", "a: [1, 2\n")
    with pytest.raises(RuntimeError, match="broken.yaml"):
        pygim.path(f).read()


def test_engine_none_is_auto(temp_dir):
    f = _write(temp_dir, "auto.yaml", "k: 1\n")
    assert pygim.path(f, engine=None).read() == {"k": 1}
    assert pygim.path(f, engine=None).engine is None
    assert pygim.path(f).read(engine=None) == {"k": 1}


def test_toml_extension_autoselects(temp_dir):
    f = _write(temp_dir, "auto.toml", 'k = 1\n')
    p = pygim.path(f)
    assert p.engine is None and p.read() == {"k": 1}


def test_json_bigint_limitation_is_loud(temp_dir):
    # simdjson's DOM cannot represent integers beyond 64 bits. Writing them is
    # valid JSON (stdlib reads it fine); our reader refuses LOUDLY rather than
    # silently truncating. Pinned as a known, documented divergence.
    import json

    p = pygim.path(temp_dir / "big.json")
    p.write({"big": 12345678901234567890123})
    assert json.loads(open(str(p)).read()) == {"big": 12345678901234567890123}
    with pytest.raises(RuntimeError, match="BIGINT"):
        p.read()
