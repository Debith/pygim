# -*- coding: utf-8 -*-
"""Tests for ``pygim.path`` — the self-reading, self-decoding PathLike."""

import os

import pytest

import pygim
from pygim import pathlike
import pathlib


def _write(temp_dir, name, text):
    p = temp_dir / name
    # Exact bytes on every platform and Python (write_text(newline=) needs
    # 3.10+): Windows text mode would otherwise write cp1252 and CRLF,
    # breaking the UTF-8 parsers and byte-size assertions.
    p.write_bytes(text.encode("utf-8"))
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
    # A genuinely absolute path on every platform ("/x/y" has no drive on
    # Windows, so std::filesystem correctly calls it relative there).
    assert pygim.path(pathlib.Path.cwd()).is_absolute() is True


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
    assert p.engine == "rapidyaml"
    assert p.read() == {"name": "test", "count": 3}


def test_engine_reports_resolution():
    # .engine is the RESOLVED engine: pin if set, else the extension.
    assert pygim.path("a.yaml").engine == "rapidyaml"   # engines are LIBRARIES
    assert pygim.path("a.toml").engine == "toml++"
    assert pygim.path("a.json").engine == "simdjson"
    assert pygim.path("a.txt").engine is None             # nothing resolves


def test_engine_pin_propagates_to_derived_paths():
    p = pygim.path("a/b.dat", engine="yaml")
    assert p.parent.engine == "rapidyaml"
    assert p.with_name("c.dat").engine == "rapidyaml"
    assert p.with_suffix(".cfg").engine == "rapidyaml"
    assert (p / "sub.dat").engine == "rapidyaml"
    assert all(x.engine == "rapidyaml" for x in p.parents)


def test_engine_pin_shown_in_repr():
    assert repr(pygim.path("x.dat", engine="yaml")) == 'file("file://x.dat", engine=rapidyaml)'
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


def _toml_oracle():
    """tomllib (3.11+ stdlib) or its tomli backport — same API."""
    try:
        import tomllib
        return tomllib
    except ModuleNotFoundError:
        return pytest.importorskip("tomli")


@pytest.mark.parametrize("doc", TOML_CORPUS)
def test_toml_read_matches_tomllib(temp_dir, doc):
    tomllib = _toml_oracle()

    f = _write(temp_dir, "diff.toml", doc)
    assert pygim.path(f).read() == tomllib.loads(doc)


TOML_WRITE_OBJS = [
    {"title": "svc", "count": 42, "ratio": 2.5, "on": True, "inf": float("inf")},
    {"tags": ["a", "b"], "nested": {"deep": {"x": 1}}, "points": [{"x": 1}, {"x": 2}]},
    {"unicode": "hätä — åäö", "empty": {}, "seq": []},
]


@pytest.mark.parametrize("obj", TOML_WRITE_OBJS)
def test_toml_write_read_roundtrip(temp_dir, obj):
    p = pygim.path(temp_dir / "rt.toml")
    p.write(obj)
    assert p.read() == obj
    assert _toml_oracle().loads((temp_dir / "rt.toml").read_text(encoding="utf-8")) == obj


def test_toml_write_datetimes_roundtrip(temp_dir):
    import datetime as dt

    obj = {
        "day": dt.date(2024, 1, 15),
        "tea": dt.time(10, 30, 15, 250000),
        "stamp": dt.datetime(2024, 1, 15, 10, 30,
                             tzinfo=dt.timezone(dt.timedelta(hours=2))),
        "naive": dt.datetime(2024, 1, 15, 10, 30),
    }
    p = pygim.path(temp_dir / "dt.toml")
    p.write(obj)
    assert p.read() == obj
    assert _toml_oracle().loads((temp_dir / "dt.toml").read_text(encoding="utf-8")) == obj


def test_toml_write_rejections_are_loud(temp_dir):
    p = pygim.path(temp_dir / "bad.toml")
    with pytest.raises(ValueError, match="must be a mapping"):
        p.write([1, 2])                       # TOML documents are tables
    with pytest.raises(ValueError, match="no null"):
        p.write({"a": None})
    with pytest.raises(ValueError, match="64-bit"):
        p.write({"a": 2**70})


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
    assert hits and all(f.engine == "rapidyaml" for f in hits)
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
    assert pygim.path(f, engine=None).engine == "rapidyaml"    # resolved by extension
    assert pygim.path(f).read(engine=None) == {"k": 1}


def test_toml_extension_autoselects(temp_dir):
    f = _write(temp_dir, "auto.toml", 'k = 1\n')
    p = pygim.path(f)
    assert p.engine == "toml++" and p.read() == {"k": 1}


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


def test_differential_oracles_installed_in_ci():
    """The differential harness must actually RUN in the merge gate.

    importorskip keeps casual local runs friendly, but a skipped oracle test
    is invisible on a green run — so in CI a missing oracle is a FAILURE,
    ensuring the gate cannot silently hollow out.
    """
    if not os.environ.get("CI"):
        pytest.skip("oracle presence only enforced on CI")
    import yaml  # noqa: F401  (PyYAML: the YAML common-ground oracle)
    _toml_oracle()  # tomllib or tomli: the TOML oracle


# --------------------------------------------------------------------------- #
# pathlib is the oracle for path semantics (the decode oracles are above)
# --------------------------------------------------------------------------- #
# NOTE: "a." and "..a" are deliberately absent — pathlib itself changed their
# stem/suffix between 3.12 and 3.14 (see test_dot_edge_semantics_are_pinned),
# so they cannot be differential-tested against a version-dependent oracle.
PATH_CORPUS = [
    "a.yaml", "a..b", ".bashrc", ".bashrc.swp", "..", ".", "a/b/", "/",
    "a//b", "archive.tar.gz", "a.b.c.d", "no_ext", "/abs/x.yml", "./rel",
    "spa ce/f.yaml", "...", "a...gz", "a/./b", "x.YAML",
]


@pytest.mark.parametrize("s", PATH_CORPUS)
def test_name_components_match_pathlib(s):
    # The class docstring claims pathlib parity; this test makes pathlib the
    # oracle for it, the same way PyYAML/tomllib are oracles for decoding.
    # (PurePosixPath on POSIX, PureWindowsPath on Windows — both must agree.)
    p, ref = pygim.path(s), pathlib.PurePath(s)
    assert p.name == ref.name, f"name({s!r})"
    assert p.stem == ref.stem, f"stem({s!r})"
    assert p.suffix == ref.suffix, f"suffix({s!r})"
    assert p.suffixes == list(ref.suffixes), f"suffixes({s!r})"
    # parts: the root component is "/" vs "\\" depending on platform strategy;
    # compare separator-normalised.
    ours = [x.replace("\\", "/") for x in p.parts]
    theirs = [x.replace("\\", "/") for x in ref.parts]
    assert ours == theirs, f"parts({s!r})"
    assert p.is_absolute() == ref.is_absolute(), f"is_absolute({s!r})"


# --------------------------------------------------------------------------- #
# Encoding contract: UTF-8 in (BOM tolerated), everything else fails LOUDLY
# --------------------------------------------------------------------------- #
def _write_bytes(temp_dir, name, data):
    p = temp_dir / name
    p.write_bytes(data)
    return p


@pytest.mark.parametrize("name,doc", [
    ("bom.yaml", b"k: 1\n"),
    ("bom.json", b'{"k": 1}'),
    ("bom.toml", b"k = 1\n"),
])
def test_utf8_bom_is_tolerated(temp_dir, name, doc):
    # What Windows Notepad writes; PyYAML/json/tomllib all tolerate it.
    f = _write_bytes(temp_dir, name, b"\xef\xbb\xbf" + doc)
    assert pygim.path(f).read() == {"k": 1}


@pytest.mark.parametrize("name,doc", [
    ("u16.yaml", "k: 1\n"),
    ("u16.json", '{"k": 1}'),
    ("u16.toml", "k = 1\n"),
])
def test_utf16_fails_loudly(temp_dir, name, doc):
    # What PowerShell `>` redirection writes. Without the explicit gate the
    # YAML engine parsed this into NUL-riddled garbage strings — found by
    # this corpus. Garbage-out is never acceptable; a clear error is.
    f = _write_bytes(temp_dir, name, doc.encode("utf-16"))
    with pytest.raises(RuntimeError, match="UTF-16|UTF8|utf-8|invalid"):
        pygim.path(f).read()


@pytest.mark.parametrize("name,doc", [
    ("l1.yaml", b"k: h\xe4t\xe4\n"),
    ("l1.json", b'{"k": "h\xe4t\xe4"}'),
    ("l1.toml", b'k = "h\xe4t\xe4"\n'),
])
def test_invalid_utf8_fails_loudly_with_filename(temp_dir, name, doc):
    # Latin-1 bytes. The YAML engine used to raise a confusing
    # UnicodeDecodeError at materialisation time — or NOTHING when the bad
    # byte sat in a comment; now every engine rejects up front, naming the file.
    f = _write_bytes(temp_dir, name, doc)
    with pytest.raises(RuntimeError, match=name.split(".")[0]):
        pygim.path(f).read()


def test_invalid_utf8_in_comment_still_fails(temp_dir):
    f = _write_bytes(temp_dir, "cmt.yaml", b"k: 1  # h\xe4t\xe4\n")
    with pytest.raises(RuntimeError, match="not valid UTF-8"):
        pygim.path(f).read()


# --------------------------------------------------------------------------- #
# Concurrency: parse ERRORS across threads (success is covered above)
# --------------------------------------------------------------------------- #
def test_concurrent_parse_errors_propagate_cleanly(temp_dir):
    # The spiciest path in the extension: several threads simultaneously hit
    # the throwing ryml error callback while the GIL is released. Each thread
    # must get its own clean Python exception — no crashes, no cross-talk.
    from concurrent.futures import ThreadPoolExecutor

    good = pygim.path(_write(temp_dir, "good.yaml", "k: 1\n"))
    bad = pygim.path(_write(temp_dir, "bad.yaml", "k: [1, 2\n"))

    def read_one(i):
        f = bad if i % 2 else good
        try:
            return f.read()
        except RuntimeError as e:
            return f"error:{'bad.yaml' in str(e)}"

    with ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(read_one, range(64)))
    assert all(r == {"k": 1} for r in results[::2])
    assert all(r == "error:True" for r in results[1::2])


# --------------------------------------------------------------------------- #
# Robustness: read() must throw-or-return on arbitrary input, never crash
# --------------------------------------------------------------------------- #
def test_nasty_scalar_corpus_never_crashes(temp_dir):
    import random

    nasty = [
        "", ":", "-", "---", "...", "{", "[", "'", '"', "\\", "\t", "%YAML 1.2",
        "&a *a", "*undefined", "!!binary xxx", "a: " + "9" * 5000,
        "a: " + "\ud800".encode("utf-8", "surrogatepass").decode("latin1"),
        "\x00".join("ab"), "🎲: 🎯", "- " * 1000, "[" * 200, "?" * 100,
    ]
    rng = random.Random(42)   # deterministic: same corpus every run
    charset = ":-[]{}#&*!|>'\"%@` \n\tabc0123456789.ä世"
    nasty += ["".join(rng.choice(charset) for _ in range(rng.randint(1, 200)))
              for _ in range(100)]

    for i, doc in enumerate(nasty):
        f = temp_dir / f"nasty_{i}.yaml"
        f.write_bytes(doc.encode("utf-8", "replace"))
        try:
            pygim.path(f).read()          # any result is fine...
        except (RuntimeError, ValueError, UnicodeDecodeError):
            pass                          # ...and any loud error is fine.


def test_dot_edge_semantics_are_pinned():
    """pathlib is an unstable oracle for these two shapes.

    CPython reversed itself between 3.12 and 3.14: 'a.' went from
    (stem 'a.', suffix '') to (stem 'a', suffix '.'), and '..a' from
    (stem '.', suffix '.a') to (stem '..a', suffix ''). A differential
    assert would therefore fail on one interpreter or another by
    construction. pygim pins the 3.12/3.13-family behaviour explicitly;
    revisit if the ecosystem settles on the 3.14 rules.
    """
    p = pygim.path("a.")
    assert (p.stem, p.suffix, p.suffixes) == ("a.", "", [])
    q = pygim.path("..a")
    # 3.12-family pathlib is internally INCONSISTENT here (suffix says '.a',
    # suffixes says []) because the two properties use different algorithms;
    # 3.14 resolved it the other way. We mirror 3.12 faithfully, wart and all.
    assert (q.stem, q.suffix, q.suffixes) == (".", ".a", [])


# --------------------------------------------------------------------------- #
# Typed file classes: the type mirrors the resolved engine
# --------------------------------------------------------------------------- #
def test_path_returns_engine_typed_subclasses():
    from pygim.pathlike import file, jsonfile, tomlfile, yamlfile

    assert isinstance(pygim.path("a.yaml"), yamlfile)
    assert isinstance(pygim.path("a.toml"), tomlfile)
    assert isinstance(pygim.path("a.json"), jsonfile)
    assert isinstance(pygim.path("a.yaml"), file)          # still a file
    assert type(pygim.path("a.txt")) is file               # unresolved: plain file


def test_typed_subclass_follows_pin_and_derivation():
    from pygim.pathlike import jsonfile, tomlfile, yamlfile

    assert isinstance(pygim.path("a.json", engine="yaml"), yamlfile)   # pin wins
    assert isinstance(pygim.path("a.yaml").with_suffix(".json"), jsonfile)
    assert isinstance(pygim.path("cfg.yaml").parent / "x.toml", tomlfile)


def test_direct_subclass_construction_pins(temp_dir):
    from pygim.pathlike import yamlfile

    f = _write(temp_dir, "legacy.dat", "k: 1\n")
    p = yamlfile(f)                                        # type == pinned engine
    assert p.engine == "rapidyaml" and isinstance(p, yamlfile)
    assert p.read() == {"k": 1}
    assert isinstance(p.with_name("other.dat"), yamlfile)  # pin travels, type too


# --------------------------------------------------------------------------- #
# JSONL engine (simdjson document stream): one document per line <-> a list
# --------------------------------------------------------------------------- #
@pytest.mark.parametrize("text", [
    '{"a": 1}\n{"b": [1, 2]}\n',
    '{"a": 1}\r\n{"b": 2}\r\n',                      # CRLF
    '{"a": 1}\n\n\n{"b": 2}',                        # blank lines, no trailing newline
    '1\n"two"\nnull\ntrue\n2.5\n[]\n{}\n',           # scalar documents
    '{"s": "line\\nbreak", "u": "\\u00e9"}\n',
])
def test_jsonl_read_matches_stdlib_per_line(temp_dir, text):
    import json

    f = _write(temp_dir, "rows.jsonl", text)
    expected = [json.loads(line) for line in text.splitlines() if line.strip()]
    assert pygim.path(f).read() == expected


def test_jsonl_empty_and_whitespace_only_read_as_empty_list(temp_dir):
    assert pygim.path(_write(temp_dir, "empty.jsonl", "")).read() == []
    assert pygim.path(_write(temp_dir, "blank.jsonl", "\n \n")).read() == []


def test_jsonl_bom_tolerated_and_utf16_refused(temp_dir):
    f = _write_bytes(temp_dir, "bom.jsonl", b"\xef\xbb\xbf" + b'{"k": 1}\n')
    assert pygim.path(f).read() == [{"k": 1}]
    g = _write_bytes(temp_dir, "u16.jsonl", '{"k": 1}\n'.encode("utf-16"))
    with pytest.raises(RuntimeError, match="UTF-8"):
        pygim.path(g).read()


def test_jsonl_parse_error_names_file_and_line(temp_dir):
    f = _write(temp_dir, "bad.jsonl", '{"a": 1}\n{"b": 2}\nnot json\n{"c": 3}\n')
    with pytest.raises(RuntimeError, match=r"bad\.jsonl, line 3"):
        pygim.path(f).read()


@pytest.mark.parametrize("rows", [
    [],
    [{"a": 1}, {"b": [1, 2, {"c": None}]}],
    [1, "two", None, True, 2.5, [], {}],
    [{"s": "multi\nline", "q": 'quo"te', "t": "true"}],
])
def test_jsonl_write_read_roundtrip_one_document_per_line(temp_dir, rows):
    import json

    p = pygim.path(temp_dir / "out.jsonl")
    p.write(rows)
    text = (temp_dir / "out.jsonl").read_text(encoding="utf-8")
    lines = text.splitlines()
    assert len(lines) == len(rows) and (text == "" or text.endswith("\n"))
    assert [json.loads(line) for line in lines] == rows     # every line is a JSON document
    assert p.read() == rows


def test_jsonl_write_requires_a_list_root(temp_dir):
    p = pygim.path(temp_dir / "out.jsonl")
    with pytest.raises(ValueError, match="list or tuple"):
        p.write({"a": 1})
    p.write(({"a": 1},))                                    # tuples are sequences too
    assert p.read() == [{"a": 1}]


def test_jsonl_engine_resolution_and_typed_class(temp_dir):
    from pygim.pathlike import jsonfile, jsonlfile

    assert isinstance(pygim.path("a.jsonl"), jsonlfile)
    assert isinstance(pygim.path("a.ndjson"), jsonlfile)
    assert not isinstance(pygim.path("a.json"), jsonlfile)
    assert not isinstance(pygim.path("a.jsonl"), jsonfile)
    assert pygim.path("a.jsonl").engine == "simdjson-ndjson"
    assert pygim.path("a.JSONL").engine == "simdjson-ndjson"
    f = _write(temp_dir, "rows.dat", '{"k": 1}\n')
    assert pygim.path(f, engine="jsonl").read() == [{"k": 1}]
    assert pygim.path(f).read(engine="ndjson") == [{"k": 1}]
    assert jsonlfile(f).read() == [{"k": 1}]
    assert pygim.path(f, engine="simdjson-ndjson").engine == "simdjson-ndjson"   # label round-trips


# --------------------------------------------------------------------------- #
# write_bytes / mkdir (pathlib parity)
# --------------------------------------------------------------------------- #
def test_write_bytes_roundtrips_raw(temp_dir):
    p = pygim.path(temp_dir / "blob.bin")
    p.write_bytes(b"\x00\xff\r\n")
    assert p.read_bytes() == b"\x00\xff\r\n"
    assert (temp_dir / "blob.bin").read_bytes() == b"\x00\xff\r\n"


def test_write_bytes_into_missing_directory_fails_loudly(temp_dir):
    with pytest.raises(RuntimeError, match="cannot open file for writing"):
        pygim.path(temp_dir / "missing" / "blob.bin").write_bytes(b"x")


def test_mkdir_semantics_match_pathlib(temp_dir):
    d = pygim.path(temp_dir / "a" / "b")
    with pytest.raises(RuntimeError):
        d.mkdir()                                            # parent missing
    d.mkdir(parents=True)
    assert d.is_dir()
    with pytest.raises(RuntimeError, match="exists"):
        d.mkdir()
    d.mkdir(exist_ok=True)                                   # idempotent
    d.mkdir(parents=True, exist_ok=True)
    (temp_dir / "file").write_text("x")
    with pytest.raises(RuntimeError, match="not a directory"):
        pygim.path(temp_dir / "file").mkdir(exist_ok=True)


# --------------------------------------------------------------------------- #
# The OPEN engine registry: one header per engine, discovered by the build.
# These tests are written once against pathlike.ENGINES, so a new engine is
# covered without new test code — and cannot land half-wired.
# --------------------------------------------------------------------------- #
def _engines():
    from pygim import pathlike

    return pathlike.ENGINES


def test_engines_record_shape():
    from pygim import pathlike

    names = [e.name for e in pathlike.ENGINES]
    assert names == sorted(names) and len(set(names)) == len(names)   # pack order = sorted stems
    for e in pathlike.ENGINES:
        assert e.name and e.label and e.doc and e.extensions
        assert all(x.startswith(".") and x == x.lower() for x in e.extensions)
        assert all(a == a.lower() for a in e.aliases)


@pytest.mark.parametrize("engine", _engines(), ids=lambda e: e.name)
def test_every_engine_is_fully_wired(engine, temp_dir):
    from pygim import pathlike

    cls = getattr(pathlike, engine.name + "file")           # the typed class exists...
    assert issubclass(cls, pathlike.file) and cls is not pathlike.file
    for ext in engine.extensions:                            # ...its extensions dispatch to it...
        p = pygim.path("x" + ext)
        assert isinstance(p, cls) and p.engine == engine.label
        assert pygim.path("x" + ext.upper()).engine == engine.label   # case-folded
    for selector in (engine.name, engine.label, *engine.aliases):   # ...every selector pins it...
        p = pygim.path("x.dat", engine=selector)
        assert isinstance(p, cls) and p.engine == engine.label
        assert pygim.path("x.dat", engine=engine.label).engine == engine.label   # label round-trips
    direct = cls(temp_dir / "x.dat")                         # ...and direct construction pins it
    assert isinstance(direct, cls) and direct.engine == engine.label
    assert isinstance(direct.with_name("y.dat"), cls)
    assert repr(direct).endswith(f'engine={engine.label})')


def test_unknown_engine_error_lists_every_engine():
    with pytest.raises(ValueError, match="unknown engine: 'xml'") as info:
        pygim.path("a.yaml", engine="xml")
    for e in _engines():
        assert f"{e.name}/{e.label}" in str(info.value)
    with pytest.raises(ValueError, match="unknown engine: 'xml'"):
        pygim.path("a.yaml").read(engine="xml")


def test_unknown_extension_error_lists_every_extension(temp_dir):
    f = _write(temp_dir, "mystery.weird", "k: v\n")
    with pytest.raises(ValueError, match="no engine for extension '.weird'") as info:
        pygim.path(f).read()
    for e in _engines():
        for ext in e.extensions:
            assert ext in str(info.value)


def test_typed_classes_are_exactly_the_registry():
    from pygim import pathlike

    typed = {name for name, obj in vars(pathlike).items()
             if isinstance(obj, type) and issubclass(obj, pathlike.file) and obj is not pathlike.file}
    assert typed == {e.name + "file" for e in pathlike.ENGINES}


def test_registry_matches_source_tree():
    """A header added to adapter/engines/ without a rebuild (or a generator that
    missed one) shows up here, not as a silent gap."""
    engines_dir = pathlib.Path(__file__).parents[2] / "src" / "_pygim_fast" / "pathlike" / "adapter" / "engines"
    if not engines_dir.is_dir():
        pytest.skip("source tree not present (installed wheel)")
    assert sorted(h.stem for h in engines_dir.glob("*.h")) == [e.name for e in _engines()]


def test_docstrings_are_derived_from_the_registry():
    from pygim import pathlike

    for e in pathlike.ENGINES:
        assert e.label in pathlike.file.__doc__ and e.name in pathlike.path.__doc__
        assert e.doc in getattr(pathlike, e.name + "file").__doc__
        assert e.doc in pathlike.file.write.__doc__


def test_stub_engine_block_is_current():
    """src/pygim/pathlike.pyi's generated block must match the built module
    (regenerate with `pygim stubs`)."""
    from pygim import _stubs

    text = _stubs.stub_path().read_text(encoding="utf-8")
    assert _stubs.render(text) == text, "stale stub: run `pygim stubs`"
    block = _stubs.engine_block()
    for e in _engines():
        assert f"class {e.name}file(file):" in block and f'"{e.label}"' in block


# --------------------------------------------------------------------------- #
# The path VALUE is a URI (RFC 3986) behind a pathlib-shaped API: normalised
# spelling, file:// URIs in, RFC rendering out.
# --------------------------------------------------------------------------- #
@pytest.mark.parametrize("text,expected", [
    ("a/b/", "a/b"), ("a//b", "a/b"), ("./rel", "rel"), ("", "."), ("a/./b", "a/b"), ("/x/y/", "/x/y"),
    ("a/../b", "a/../b"),          # pathlib keeps ".." — RFC dot-segment removal is never implicit
])
def test_fspath_is_pathlib_normalised(text, expected):
    if os.name == "nt":
        expected = expected.replace("/", "\\")
    assert os.fspath(pygim.path(text)) == expected
    assert os.fspath(pygim.path(text)) == str(pathlib.PurePath(text))   # pathlib is the oracle


def test_equality_is_value_equality_not_spelling():
    assert pygim.path("a/b/") == pygim.path("a//b") == pygim.path("./a/b")
    assert hash(pygim.path("a/b/")) == hash(pygim.path("a/b"))
    assert pygim.path("a/b") != pygim.path("a/c")


@pytest.mark.skipif(os.name == "nt", reason="POSIX URI forms")
def test_uri_renders_absolute_paths_per_rfc_3986():
    assert pygim.path("/tmp/a b/é.yaml").uri == "file:///tmp/a%20b/%C3%A9.yaml"
    assert pygim.path("/x/../y.json").uri == "file:///x/../y.json"          # ".." kept, like pathlib.as_uri
    assert pygim.path("/a#b?c").uri == "file:///a%23b%3Fc"                   # reserved characters encoded
    assert pygim.path("/a:b@c").uri == "file:///a:b@c"                       # ':' and '@' are pchars (RFC), unencoded
    assert pygim.path("some.yaml").uri == "file://some.yaml"                 # relative: legacy spelling kept


@pytest.mark.skipif(os.name == "nt", reason="POSIX URI forms")
@pytest.mark.parametrize("uri,expected", [
    ("file:///tmp/a%20b/x.yaml", "/tmp/a b/x.yaml"),
    ("file://localhost/tmp/x.yaml", "/tmp/x.yaml"),        # 'localhost' authority is dropped
    ("FILE:///tmp/x.yaml", "/tmp/x.yaml"),                 # scheme is case-insensitive
    ("file:/tmp/x.yaml", "/tmp/x.yaml"),                   # RFC 8089 minimal form
    ("file:///tmp//a/./b/", "/tmp/a/b"),                   # decoded, then parsed like any path
    ("file://server/share/x.toml", "//server/share/x.toml"),   # foreign host: pathlib's '//host' root form on POSIX
    ("file:///tmp/%C3%A9.yaml", "/tmp/é.yaml"),
])
def test_file_uris_are_accepted_as_input(uri, expected):
    p = pygim.path(uri)
    assert os.fspath(p) == expected
    assert p.is_absolute()
    if hasattr(pathlib.Path, "from_uri"):                  # Python >= 3.13: pathlib is the oracle here too
        assert os.fspath(p) == str(pathlib.Path.from_uri(uri))


def test_file_uri_input_dispatches_by_extension_and_round_trips():
    p = pygim.path("file:///tmp/notes/config.yaml")
    assert p.engine == "rapidyaml" and isinstance(p, pathlike.yamlfile)
    assert p.uri == "file:///tmp/notes/config.yaml"
    assert pygim.path(p.uri) == p


@pytest.mark.parametrize("bad", ["file:x.yaml", "file://", "file:"])
def test_relative_file_uris_are_rejected(bad):
    with pytest.raises(ValueError, match="URI is not absolute"):
        pygim.path(bad)


def test_other_schemes_are_rejected_not_silently_treated_as_paths():
    with pytest.raises(ValueError, match="unsupported URI scheme 's3'"):
        pygim.path("s3://bucket/x.yaml")
    with pytest.raises(ValueError, match="unsupported URI scheme 'https'"):
        pygim.path("https://example.org/x.json")
    assert os.fspath(pygim.path("note:x.yaml")) == "note:x.yaml"   # a colon alone is not a URL


def test_uri_input_reads_through_the_engine(temp_dir):
    f = _write(temp_dir, "cfg.toml", "k = 1\n")
    p = pygim.path(pathlib.Path(f).as_uri())
    assert p.read() == {"k": 1} and p.engine == "toml++"


def test_with_name_and_with_suffix_validate_like_pathlib():
    with pytest.raises(ValueError):
        pygim.path("a/b.yaml").with_name("")
    with pytest.raises(ValueError):
        pygim.path("a/b.yaml").with_name("x/y")
    with pytest.raises(ValueError):
        pygim.path("a/b.yaml").with_suffix("json")          # no leading dot
    with pytest.raises(ValueError):
        pygim.path("/").with_name("x")                       # the anchor has no name
    assert os.fspath(pygim.path("a/b.tar.gz").with_suffix(".bz2")) == "a/b.tar.bz2"
    assert os.fspath(pygim.path("a/b.tar.gz").with_suffix("")) == "a/b.tar"


def test_parity_proofs_are_current():
    """tests/static/pathlike_parity_proofs.cpp is generated from pathlib
    (gen_pathlike_parity_proofs.py); it must match this interpreter's pathlib."""
    static = pathlib.Path(__file__).parents[1] / "static"
    if not static.is_dir():
        pytest.skip("source tree not present (installed wheel)")
    import importlib.util

    spec = importlib.util.spec_from_file_location("gen_parity", static / "gen_pathlike_parity_proofs.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert mod.render() == (static / "pathlike_parity_proofs.cpp").read_text(encoding="utf-8"), \
        "stale: run tests/static/gen_pathlike_parity_proofs.py"


# --------------------------------------------------------------------------- #
# pathlib is the oracle for the whole path ALGEBRA of the native strategy:
# str(), parent/parents, joining, with_*, equality, as_uri, from_uri.
# (The C++ parity proofs cover both strategies at compile time; these assert the
# Python-facing behaviour against the interpreter's own pathlib at run time.)
# --------------------------------------------------------------------------- #
ALGEBRA_CORPUS = PATH_CORPUS + [
    "", "a/b/c.yaml", "/a/b/c", "dir/", "/x/y/", "a/../b", "sub/.hidden.tar.gz", "///triple", "//net/share/x",
    "spa ce/f.yaml", "x.Y.z", "a b/c d.e",
]


@pytest.mark.parametrize("s", ALGEBRA_CORPUS)
def test_str_parent_and_parents_match_pathlib(s):
    p, ref = pygim.path(s), pathlib.PurePath(s)
    assert os.fspath(p) == str(ref), f"str({s!r})"
    assert os.fspath(p.parent) == str(ref.parent), f"parent({s!r})"
    # parents(): ours lists ancestors up to the anchor; pathlib additionally ends
    # a relative path's list with "." — documented divergence, everything else equal.
    ours = [os.fspath(x) for x in p.parents]
    theirs = [str(x) for x in ref.parents if str(x) != "."]
    assert ours == theirs, f"parents({s!r})"


@pytest.mark.parametrize("base,other", [
    ("a/b", "c"), ("a/b", "/x"), ("/a/b", "c/d"), ("", "x"), ("a", "b/c/"), ("/", "x"), ("a/b", ""),
    ("a/b", "."), ("a/b", ".."), ("a//b", "c//d"), ("/a", "/"), ("x", "y/../z"), ("spa ce", "f g"),
])
def test_joining_matches_pathlib(base, other):
    assert os.fspath(pygim.path(base) / other) == str(pathlib.PurePath(base) / other)
    assert os.fspath(other / pygim.path(base)) == str(other / pathlib.PurePath(base))
    assert os.fspath(pygim.path(base).joinpath(other, "z")) == str(pathlib.PurePath(base).joinpath(other, "z"))


@pytest.mark.parametrize("s", ["a/b.yaml", "a/b.tar.gz", ".bashrc", "no_ext", "/", "a/b/", "x", "/abs/x.yml", "dir/"])
@pytest.mark.parametrize("method,arg", [
    ("with_name", "z.txt"), ("with_name", ".."), ("with_name", ""), ("with_name", "."), ("with_name", "a/b"),
    ("with_suffix", ".json"), ("with_suffix", ""), ("with_suffix", "json"), ("with_suffix", "."),
    ("with_stem", "q"), ("with_stem", ""),
])
def test_with_name_suffix_stem_match_pathlib_including_errors(s, method, arg):
    try:
        expected = str(getattr(pathlib.PurePath(s), method)(arg))
    except ValueError:
        expected = ValueError
    if expected is ValueError:
        with pytest.raises(ValueError):
            getattr(pygim.path(s), method)(arg)
    else:
        assert os.fspath(getattr(pygim.path(s), method)(arg)) == expected, f"{method}({s!r}, {arg!r})"


@pytest.mark.skipif(os.name == "nt", reason="PureWindowsPath compares case-insensitively; ours is case-sensitive by decision")
@pytest.mark.parametrize("a", ALGEBRA_CORPUS)
@pytest.mark.parametrize("b", ["a/b", "a//b/", "./a/b", "a/B", "/a/b", ".", "", "a.yaml", "a/../b"])
def test_equality_and_hash_match_pathlib(a, b):
    same = pathlib.PurePath(a) == pathlib.PurePath(b)
    assert (pygim.path(a) == pygim.path(b)) == same, f"{a!r} == {b!r}"
    if same:
        assert hash(pygim.path(a)) == hash(pygim.path(b))


@pytest.mark.skipif(os.name == "nt", reason="POSIX URI forms")
@pytest.mark.parametrize("s", ["/abs/x.yml", "/tmp/a b/é.yaml", "/x/../y.json", "/a/b/", "/", "/dir/.hidden.tar.gz"])
def test_uri_of_absolute_paths_matches_pathlib_as_uri(s):
    # Where the RFC pchar set and pathlib's quote() agree (no ':' '@' or sub-delims in the path).
    assert pygim.path(s).uri == pathlib.PurePath(s).as_uri()


@pytest.mark.parametrize("s", ["a/b.yaml", "rel", ".", ""])
def test_uri_of_relative_paths_is_a_decision_not_an_error(s):
    with pytest.raises(ValueError):
        pathlib.PurePath(s).as_uri()          # pathlib refuses
    assert pygim.path(s).uri.startswith("file://")   # ours keeps the legacy spelling (user decision)


@pytest.mark.skipif(not hasattr(pathlib.Path, "from_uri"), reason="pathlib.Path.from_uri is Python 3.13+")
@pytest.mark.skipif(os.name == "nt", reason="POSIX URI forms")
@pytest.mark.parametrize("uri", [
    "file:///tmp/a%20b/x.yaml", "file://localhost/tmp/x.yaml", "file:/tmp/x.yaml", "file:///tmp//a/./b/",
    "file://server/share/x.toml", "file:///tmp/%C3%A9.yaml", "file:///", "file:///a/../b",
])
def test_file_uri_input_matches_pathlib_from_uri(uri):
    assert os.fspath(pygim.path(uri)) == str(pathlib.Path.from_uri(uri))


@pytest.mark.skipif(not hasattr(pathlib.Path, "from_uri"), reason="pathlib.Path.from_uri is Python 3.13+")
@pytest.mark.parametrize("bad", ["file:x.yaml", "file://", "file:"])
def test_relative_file_uri_rejection_matches_pathlib(bad):
    with pytest.raises(ValueError):
        pathlib.Path.from_uri(bad)
    with pytest.raises(ValueError):
        pygim.path(bad)
