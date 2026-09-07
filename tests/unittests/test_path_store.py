# -*- coding: utf-8 -*-
"""The flyweight store behind pygim.path(): equal paths made through path() are the
same object while anything holds them; file() is the raw constructor; stores are
plain objects (IoC-friendly) and use_store() scopes a block to one of them."""

import gc
import os
import pathlib

import pytest

import pygim
from pygim import pathlike
from pygim.ioc import Container

P = pathlib.PurePath


@pytest.fixture
def fresh():
    """A private store made current for the test, so identity assertions are isolated."""
    st = pathlike.PathStore()
    with pathlike.use_store(st):
        yield st


# ── identity ───────────────────────────────────────────────────────────────
def test_path_returns_the_same_object_for_the_same_value(fresh):
    a = pygim.path("a/b.yaml")
    assert pygim.path("a/b.yaml") is a
    assert pygim.path("a//b.yaml/") is a          # spellings merge: one value, one object
    assert pygim.path("./a/b.yaml") is a
    assert isinstance(a, pathlike.yamlfile)        # the typed class is kept
    assert pygim.path("/a/b.yaml") is not a        # a different value


def test_derived_paths_are_interned_too(fresh):
    p = pygim.path("a/b/c.json")
    assert p.parent is pygim.path("a/b")
    assert p.parent.parent is pygim.path("a")
    assert (p.parent / "d.toml") is pygim.path("a/b/d.toml")
    assert p.with_suffix(".yaml") is pygim.path("a/b/c.yaml")
    assert pygim.path("a") / "b" / "c.json" is p
    assert p.parents[0] is p.parent


def test_the_class_constructor_is_the_raw_value_constructor(fresh):
    raw = pathlike.file("a/b")
    assert raw == pygim.path("a/b") and raw is not pygim.path("a/b")
    assert pathlike.file("a/b") is not raw


def test_pinned_paths_bypass_the_store(fresh):
    plain = pygim.path("x.json")
    pinned = pygim.path("x.json", engine="toml")
    assert isinstance(pinned, pathlike.tomlfile) and pinned is not plain
    assert pygim.path("x.json", engine="toml") is not pinned   # a pin is per object
    assert pygim.path("x.json") is plain                       # and does not disturb the slot


def test_equality_and_hashing_are_unchanged(fresh):
    a, b = pygim.path("q/r"), pathlike.file("q/r")
    assert a == b and hash(a) == hash(b) and {a: 1}[b] == 1


# ── lifetime ───────────────────────────────────────────────────────────────
def test_the_store_holds_objects_weakly(fresh):
    objs = [pygim.path(f"w/{i}.txt") for i in range(50)]
    assert fresh.stats()["live"] == 50
    rows = fresh.stats()["rows"]
    del objs
    gc.collect()
    assert fresh.stats()["live"] == 0            # objects died with their last reference
    assert fresh.stats()["rows"] == rows         # the table keeps the values
    again = pygim.path("w/7.txt")
    assert fresh.stats()["live"] == 1 and again.name == "7.txt"


def test_a_stored_object_outlives_nothing_but_its_owners(fresh):
    keep = pygim.path("keep/me")
    tmp = pygim.path("drop/me")
    del tmp
    gc.collect()
    assert fresh.stats()["live"] == 1
    assert pygim.path("keep/me") is keep


# ── stores as objects ──────────────────────────────────────────────────────
def test_use_store_scopes_identity_to_the_block():
    outer = pygim.path("scope/x")
    private = pathlike.PathStore()
    with pathlike.use_store(private) as active:
        assert active is private and pathlike.store() is private
        inner = pygim.path("scope/x")
        assert inner is not outer and inner == outer
        assert pygim.path("scope/x") is inner
        with pathlike.use_store(pathlike.PathStore()):
            assert pygim.path("scope/x") is not inner
        assert pathlike.store() is private
    assert pathlike.store() is not private
    assert pygim.path("scope/x") is outer


def test_store_path_interns_in_that_store_regardless_of_the_current_one():
    st = pathlike.PathStore()
    p = st.path("direct/one")
    assert st.path("direct/one") is p
    assert st.stats()["rows"] >= 1 and st.stats()["live"] == 1
    assert pygim.path("direct/one") is not p     # the current store is a different one


def test_use_store_rejects_non_stores():
    with pytest.raises(TypeError):
        pathlike.use_store(object())


def test_an_ioc_container_owns_a_store(fresh):
    container = Container()
    container.register(pathlike.PathStore, pathlike.PathStore, lifecycle="singleton")
    st = container.resolve(pathlike.PathStore)
    assert container.resolve(pathlike.PathStore) is st
    with pathlike.use_store(st):
        p = pygim.path("ioc/a")
        assert pygim.path("ioc/a") is p
    assert pygim.path("ioc/a") is not p          # back in the test's own store
    assert st.stats()["live"] == 1


# ── PathSet and the store share a table ────────────────────────────────────
def test_pathset_over_the_store_meets_its_objects_without_reinterning(fresh):
    ps = pathlike.PathSet(["s/a.yaml", "s/b.json", "t/c"], store=fresh)
    assert pygim.path("s/a.yaml") in ps
    assert pathlike.file("t/c") in ps and pathlike.file("t/d") not in ps
    rows_before = fresh.stats()["rows"]
    v = ps[0]
    f = v.to_file()
    assert f is pygim.path("s/a.yaml") and f is v.to_file()
    assert isinstance(f, pathlike.yamlfile)
    assert fresh.stats()["rows"] == rows_before  # nothing new was interned
    filtered = ps.filter_suffix(".json")
    assert [w.to_file() for w in filtered] == [pygim.path("s/b.json")]


def test_views_from_a_foreign_table_intern_into_the_current_store(fresh):
    ps = pathlike.PathSet(["f/one", "f/two"])         # its own table
    assert ps[1].to_file() is pygim.path("f/two")
    assert ps[1].to_file() is ps[1].to_file()


def test_glob_results_are_interned(fresh, tmp_path):
    for name in ("a.yaml", "b.yaml"):
        (tmp_path / name).write_text("x: 1")
    d = pygim.path(tmp_path)
    hits = d.glob("*.yaml")
    assert hits == sorted(hits, key=os.fspath)
    assert all(h is pygim.path(os.fspath(h)) for h in hits)
    assert hits[0].parent is d


# ── derived paths by row agree with the value route ────────────────────────
def _corpus():
    from test_pathset_proto import corpus, EDGE
    return corpus(300) + EDGE + ["a/..", "../x", "a/./b", "//net/share", "/", "x/y/z.tar.gz"]


@pytest.mark.parametrize("seg", ["x", "..", ".", "", "a/b", "/abs", "x.yaml", "d:e"])
def test_join_by_row_matches_the_value_route(fresh, seg):
    for s in _corpus():
        p = pygim.path(s)
        by_row = p / seg
        by_value = pathlike.file(s) / seg
        assert by_row == by_value and os.fspath(by_row) == os.fspath(by_value), (s, seg)
        assert by_row is pygim.path(os.fspath(by_value)), (s, seg)     # and it is the interned object
        assert type(by_row) is type(by_value)
    p = pygim.path("j/k")
    assert os.fspath(p.joinpath("a", "b/c", "d")) == str(P("j/k/a/b/c/d")) and p.joinpath("a", "b") is pygim.path("j/k/a/b")


def test_parent_and_parents_by_row_match_the_value_route(fresh):
    for s in _corpus():
        p = pygim.path(s)
        raw = pathlike.file(s)
        assert p.parent == raw.parent and os.fspath(p.parent) == os.fspath(raw.parent), s
        assert p.parent is pygim.path(os.fspath(raw.parent)), s
        assert [os.fspath(x) for x in p.parents] == [os.fspath(x) for x in raw.parents], s
        assert all(a is pygim.path(os.fspath(b)) for a, b in zip(p.parents, raw.parents)), s


def test_pinned_paths_derive_by_value_and_keep_the_pin(fresh):
    p = pygim.path("pin/a/b.json", engine="toml")
    assert isinstance(p.parent, pathlike.tomlfile) and isinstance(p / "c.yaml", pathlike.tomlfile)
    assert p.parent is not pygim.path("pin/a")           # pinned objects are never the interned one
    assert pygim.path("pin/a") is pygim.path("pin/a")


def test_a_copied_token_in_a_raw_object_is_not_trusted(fresh):
    p = pygim.path("tok/a/b")
    raw = pathlike.file("other/x")
    assert raw.parent == pathlike.file("other") and pygim.path("tok/a/b").parent is p.parent


def test_stats_keys(fresh):
    held = pygim.path("k/v")
    st = fresh.stats()
    assert set(st) == {"rows", "segments", "live", "table_bytes", "slot_bytes", "bytes"}
    assert st["live"] == 1 and st["slot_bytes"] >= 8
    assert st["bytes"] == st["table_bytes"] + st["slot_bytes"]     # the same total key on every component
    ps = pathlike.PathSet(["k/v"])
    assert ps.stats()["bytes"] == ps.stats()["table_bytes"] + ps.stats()["member_bytes"]
    assert "PathStore(" in repr(fresh)
