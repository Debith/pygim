# -*- coding: utf-8 -*-
"""Tests for the pathlike PathSet prototype: many paths as one table, views, filters
and set algebra."""

import os
import pathlib
import random

import pytest

import pygim
from pygim import pathlike

P = pathlib.PurePath


def corpus(n, seed=1):
    rng = random.Random(seed)
    dirs = ["home", "var", "usr", "opt", "srv", "data", "projects", "tmp"]
    exts = [".yaml", ".json", ".toml", ".jsonl", ".txt", ".md", ".yml", ""]
    out = []
    for i in range(n):
        depth = rng.randint(1, 6)
        parts = [rng.choice(dirs)] + [f"d{rng.randrange(50)}" for _ in range(depth - 1)]
        out.append(("/" if rng.random() < 0.5 else "") + "/".join(parts + [f"file{i}{rng.choice(exts)}"]))
    return out


EDGE = ["", ".", "/", "a", "a/b.yaml", "/a/b/c.tar.gz", "a//b/./c/", "./x", "a/../b", ".bashrc", "dir/", "spa ce/f g.txt"]


@pytest.fixture(scope="module")
def strs():
    return corpus(2000)


@pytest.fixture(scope="module")
def ps(strs):
    return pathlike.PathSet(strs)


# ── construction and membership ───────────────────────────────────────────

def test_len_and_dedup(strs):
    ps = pathlike.PathSet(strs + strs)
    assert len(ps) == len(set(map(str, map(P, strs))))


def test_spellings_normalise_to_one_row():
    ps = pathlike.PathSet(["a/b", "a//b/", "./a/b", "a/./b"])
    assert len(ps) == 1 and os.fspath(ps[0]) == str(P("a/b"))
    assert "a/b" in ps and "a//b" in ps and P("a/b") in ps and b"a/b" in ps
    assert "a/c" not in ps and pygim.path("a/b") in ps and pygim.path("x") not in ps


def test_views_render_like_pathlib(strs, ps):
    for s, v in zip(strs, ps):
        assert os.fspath(v) == str(P(s)) == str(v)


@pytest.mark.parametrize("s", EDGE)
def test_edge_spellings_match_file(s):
    ps = pathlike.PathSet([s])
    v = ps[0]
    f = pygim.path(s)
    assert os.fspath(v) == os.fspath(f)
    assert v.name == f.name and v.stem == f.stem and v.suffix == f.suffix and v.suffixes == f.suffixes
    assert os.fspath(v.parent) == os.fspath(f.parent)
    assert [os.fspath(x) for x in v.parents] == [os.fspath(x) for x in f.parents]
    assert v.is_absolute() == f.is_absolute() and v.uri == f.uri and v.engine == f.engine
    assert v == f and f == v and hash(v) == hash(f)


def test_name_components_match_file(strs, ps):
    for s, v in zip(strs, ps):
        f = pygim.path(s)
        assert (v.name, v.stem, v.suffix) == (f.name, f.stem, f.suffix)
        assert v.depth == len(P(s).parts) - (1 if P(s).anchor else 0)   # named components below the anchor


def test_parent_is_a_view_in_the_same_table(ps):
    v = ps[0]
    p = v.parent
    assert isinstance(p, pathlike.fileview)
    assert os.fspath(p) == os.fspath(pygim.path(os.fspath(v)).parent)


def test_view_equality_and_hash_across_tables_and_with_file():
    a = pathlike.PathSet(["x/y.yaml", "q"])
    b = pathlike.PathSet(["q", "x/y.yaml", "z"])
    assert a[0] == b[1] and a[1] == b[0] and a[0] != b[0]
    assert hash(a[0]) == hash(b[1]) == hash(pygim.path("x/y.yaml"))
    assert len({a[0], b[1], pygim.path("x/y.yaml"), pathlike.file("x//y.yaml")}) == 1
    assert b[2] not in a and b[1] in a


def test_to_file_is_typed_by_engine():
    ps = pathlike.PathSet(["cfg.yaml", "data.json", "plain"])
    assert isinstance(ps[0].to_file(), pathlike.yamlfile)
    assert isinstance(ps[1].to_file(), pathlike.jsonfile)
    assert type(ps[2].to_file()) is pathlike.file
    assert ps[0].engine == "rapidyaml" and ps[2].engine is None


def test_add_and_extend_with_files_and_views():
    ps = pathlike.PathSet()
    ps.add("a")
    ps.add(pygim.path("b/c.toml"))
    other = pathlike.PathSet(["d", "a"])
    ps.extend(other)
    ps.extend(["e", b"f"])
    assert ps.to_list() == [str(P(x)) for x in ["a", "b/c.toml", "d", "e", "f"]]


# ── iteration: fresh views, and the reusing cursor ────────────────────────

def test_iter_yields_fresh_views(ps):
    views = list(ps)
    assert len(views) == len(ps) and len({id(v) for v in views}) == len(views)
    assert [os.fspath(v) for v in views] == ps.to_list()


def test_scan_reuses_one_object(ps):
    ids = set()
    seen = []
    for v in ps.scan():
        ids.add(id(v))
        seen.append(os.fspath(v))
    assert len(ids) == 1 and seen == ps.to_list()


def test_views_outlive_their_set():
    v = pathlike.PathSet(["keep/me.yaml"])[0]
    import gc
    gc.collect()
    assert os.fspath(v) == str(P("keep/me.yaml")) and v.name == "me.yaml"


# ── value filters and set algebra ─────────────────────────────────────────

def test_filter_suffix_and_name(strs, ps):
    yaml = ps.filter_suffix(".yaml")
    assert yaml.to_list() == [str(P(s)) for s in strs if P(s).suffix == ".yaml"]
    assert all(v.suffix == ".yaml" for v in yaml)
    glob = ps.filter_name("file1*.json")
    assert glob.to_list() == [str(P(s)) for s in strs if P(s).name.startswith("file1") and P(s).suffix == ".json"]
    assert ps.filter_absolute().to_list() == [str(P(s)) for s in strs if P(s).is_absolute()]


def test_algebra_same_table(strs, ps):
    a = ps.filter_suffix(".yaml")
    b = ps.filter_absolute()
    expect = lambda pred: sorted(str(P(s)) for s in strs if pred(P(s)))  # noqa: E731
    assert sorted((a | b).to_list()) == expect(lambda p: p.suffix == ".yaml" or p.is_absolute())
    assert sorted((a & b).to_list()) == expect(lambda p: p.suffix == ".yaml" and p.is_absolute())
    assert sorted((a - b).to_list()) == expect(lambda p: p.suffix == ".yaml" and not p.is_absolute())


def test_algebra_across_tables():
    a = pathlike.PathSet(["x", "y/z.yaml", "w"])
    b = pathlike.PathSet(["w", "q", "y//z.yaml"])
    assert sorted((a | b).to_list()) == sorted(str(P(s)) for s in ["x", "y/z.yaml", "w", "q"])
    assert sorted((a & b).to_list()) == sorted(str(P(s)) for s in ["y/z.yaml", "w"])
    assert (a - b).to_list() == ["x"]
    assert (b - a).to_list() == ["q"]


def test_stats_show_the_flyweight(strs, ps):
    st = ps.stats()
    assert st["members"] == len(ps) and st["rows"] >= st["members"]
    # every distinct component once: far fewer segments than path components
    assert st["segments"] < sum(len(P(s).parts) for s in strs)
    assert (st["table_bytes"] + st["member_bytes"]) / len(ps) < 120


def test_counts_agree_with_the_built_sets(strs, ps):
    a = ps.filter_suffix(".yaml")
    b = ps.filter_absolute()
    assert a.count_union(b) == len(a | b) and a.count_intersection(b) == len(a & b) and a.count_difference(b) == len(a - b)
    assert b.count_difference(a) == len(b - a) and ps.count_union(ps) == len(ps) and ps.count_intersection(a) == len(a)
    other = pathlike.PathSet(strs[::3] + ["only/here"])            # another table: mapped, not copied
    assert ps.count_intersection(other) == len(ps & other) == len(strs[::3])
    assert ps.count_union(other) == len(ps | other) and other.count_difference(ps) == len(other - ps) == 1
    assert pathlike.PathSet().count_union(ps) == len(ps) and pathlike.PathSet().count_intersection(ps) == 0
