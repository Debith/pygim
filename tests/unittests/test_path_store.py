# -*- coding: utf-8 -*-
"""pygim.path is a handle on a row of a PathStore's table: the constructor interns,
`store=` chooses the table once and derived paths inherit it, equality and hashing
are by row (across stores by chain), and no object identity is promised."""

import gc
import os
import pathlib

import pytest

import pygim
from pygim import pathlike

P = pathlib.PurePath


@pytest.fixture
def store():
    return pathlike.PathStore()


# ── one class, typed by engine ─────────────────────────────────────────────
def test_path_is_the_class_and_typed_by_extension():
    p = pygim.path("a/b.yaml")
    assert isinstance(p, pygim.path) and isinstance(p, pathlike.yamlpath) and pygim.path is pathlike.path
    assert type(pygim.path("a/b.json")) is pathlike.jsonpath and type(pygim.path("a/b")) is pathlike.path
    assert type(p.parent) is pathlike.path and type(p / "c.toml") is pathlike.tomlpath


def test_typed_constructors_pin_and_reject_engine():
    y = pathlike.yamlpath("notes.txt")
    assert y.engine == "rapidyaml" and type(y / "z.json") is pathlike.yamlpath   # the pin is inherited
    assert type(pygim.path("x.dat", engine="json")) is pathlike.jsonpath
    with pytest.raises(TypeError, match="pins its own engine"):
        pathlike.yamlpath("x", engine="json")


def test_a_python_subclass_constructs_the_ordinary_way():
    class Mine(pygim.path):
        pass
    m = Mine("k/l.yaml")
    assert type(m) is Mine and isinstance(m, pygim.path) and m.engine == "rapidyaml" and os.fspath(m) == str(P("k/l.yaml"))


# ── equality by row, not identity ──────────────────────────────────────────
def test_spellings_collapse_and_no_identity_is_promised():
    a, b = pygim.path("a/b.yaml"), pygim.path("a//b.yaml/")
    assert a == b and hash(a) == hash(b) and a is not b                 # two handles, one row
    assert len({a, b, pygim.path("./a/b.yaml")}) == 1
    assert a != pygim.path("/a/b.yaml")


def test_equality_across_stores_is_by_chain(store):
    p = pygim.path("q/r.yaml")
    q = pygim.path("q/r.yaml", store=store)
    assert p == q and q == p and hash(p) == hash(q) and p.store != q.store
    assert q != pygim.path("q/s.yaml", store=store)


# ── stores: a lifetime chosen once ─────────────────────────────────────────
def test_store_argument_and_inheritance(store):
    p = pygim.path("a/b/c.yaml", store=store)
    assert p.store == store and store.stats()["rows"] == 4                 # ".", a, a/b, a/b/c.yaml
    assert p.parent.store == store and (p / "d").store == store and p.with_suffix(".json").store == store
    assert all(x.store == store for x in p.parents)
    assert pygim.path("a/b/c.yaml").store == pathlike.default_store() and pygim.path("z").store != store


def test_a_store_lives_as_long_as_a_handle_does():
    st = pathlike.PathStore()
    p = pygim.path("keep/me.yaml", store=st)
    rows = st.stats()["rows"]
    del st
    gc.collect()
    assert p.name == "me.yaml" and p.store.stats()["rows"] == rows        # the handle kept the table


def test_store_rejects_non_stores():
    with pytest.raises(TypeError, match="PathStore"):
        pygim.path("x", store=object())


def test_stats_reserve_repr(store):
    store.reserve(100)
    pygim.path("k/v", store=store)
    st = store.stats()
    assert set(st) == {"rows", "segments", "bytes"} and st["rows"] == 3 and st["bytes"] > 0
    assert "PathStore(" in repr(store) and store == store and store != pathlike.PathStore()


# ── PathSet and paths share a table ────────────────────────────────────────
def test_pathset_over_the_store_meets_its_paths(store):
    ps = pathlike.PathSet(["s/a.yaml", "s/b.json", "t/c"], store=store)
    p = pygim.path("s/a.yaml", store=store)
    assert p in ps and pygim.path("t/c") in ps and pygim.path("t/d") not in ps
    assert ps[0] == p and ps[0].store == store and isinstance(ps[0], pathlike.yamlpath)
    assert store.stats()["rows"] == ps.stats()["rows"]
    assert [v.name for v in ps.filter_suffix(".json")] == ["b.json"]


def test_glob_results_share_the_table(tmp_path):
    for name in ("a.yaml", "b.yaml"):
        (tmp_path / name).write_text("x: 1")
    st = pathlike.PathStore()
    d = pygim.path(tmp_path, store=st)
    hits = d.glob("*.yaml")
    assert hits == sorted(hits, key=os.fspath) and all(h.store == st for h in hits)
    assert all(isinstance(h, pathlike.yamlpath) for h in hits) and hits[0].parent == d
    assert len(d.pathset("*.yaml")) == 2 and d.pathset("*.yaml").stats()["rows"] == st.stats()["rows"]


# ── the row route agrees with pathlib ──────────────────────────────────────
def _corpus():
    from test_pathset_table import corpus, EDGE
    return corpus(300) + EDGE + ["a/..", "../x", "a/./b", "//net/share", "/", "x/y/z.tar.gz"]


@pytest.mark.parametrize("seg", ["x", "..", ".", "", "a/b", "/abs", "x.yaml", "d:e"])
def test_join_matches_pathlib(seg):
    for s in _corpus():
        p = pygim.path(s)
        want = P(s) / seg
        got = p / seg
        assert os.fspath(got) == str(want), (s, seg)
        assert got == pygim.path(str(want)) and hash(got) == hash(pygim.path(str(want)))
    p = pygim.path("j/k")
    assert os.fspath(p.joinpath("a", "b/c", "d")) == str(P("j/k/a/b/c/d"))


def test_parent_and_parents_match_pathlib():
    for s in _corpus():
        p = pygim.path(s)
        assert os.fspath(p.parent) == str(P(s).parent), s
        assert [os.fspath(x) for x in p.parents] == [str(x) for x in P(s).parents if str(x) != "."], s
