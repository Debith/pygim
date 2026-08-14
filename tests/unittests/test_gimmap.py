"""The mapping-toolkit Python surface: the gimdict() factory and its family.

test_gimdict.py pins the PRESERVED API; this file covers what the toolkit
added: the family base (gimmap), the frozen variant, transitions, merge
returning frozen, engine gating, and the str-key contract.
"""

from __future__ import annotations

from collections.abc import Mapping, MutableMapping

import pytest

from pygim import utils


def test_factory_products_are_one_family():
    d = utils.gimdict({"a": 1})
    f = utils.gimdict({"a": 1}, frozen=True)
    assert isinstance(d, utils.gimmap)
    assert isinstance(f, utils.gimmap)
    assert isinstance(d, MutableMapping)
    assert isinstance(f, Mapping)
    assert not isinstance(f, MutableMapping)


def test_frozen_has_no_mutation_surface():
    f = utils.gimdict({"a": 1}, frozen=True)
    with pytest.raises(TypeError):
        f["b"] = 2
    with pytest.raises(TypeError):
        del f["a"]
    assert not hasattr(f, "set")
    assert not hasattr(f, "merge_in")


def test_frozen_reads_and_equality():
    f = utils.gimdict({"b": 2, "a": 1}, frozen=True)
    assert f["a"] == 1
    assert f.get("missing", 42) == 42      # frozen get: normal default semantics
    assert "a" in f and "zz" not in f
    assert len(f) == 2
    assert f == {"a": 1, "b": 2}
    assert f.to_dict() == {"a": 1, "b": 2}


def test_frozen_is_hashable_like_tuple():
    f1 = utils.gimdict({"a": 1, "b": 2}, frozen=True)
    f2 = utils.gimdict({"b": 2, "a": 1}, frozen=True)   # same content
    assert hash(f1) == hash(f2)
    assert f1 == f2
    unhashable = utils.gimdict({"a": [1, 2]}, frozen=True)
    with pytest.raises(TypeError):
        hash(unhashable)


def test_freeze_thaw_roundtrip():
    d = utils.gimdict({"a": 1})
    f = d.freeze()
    assert isinstance(f, Mapping) and not isinstance(f, MutableMapping)
    d["b"] = 2                             # the source stays usable and separate
    assert len(f) == 1 and len(d) == 2
    t = f.thaw()
    t["c"] = 3
    assert t.to_dict() == {"a": 1, "c": 3}
    assert f.to_dict() == {"a": 1}         # thaw is a copy


def test_merge_returns_frozen_snapshot():
    a = utils.gimdict({"hp": 10})
    b = utils.gimdict({"hp": 2})
    folded = a | b
    assert isinstance(folded, utils.frozen_gimmap)
    assert folded["hp"] == 12              # int type-default: sum
    with pytest.raises(TypeError):
        folded["hp"] = 0                   # a merged result IS a snapshot
    assert a.to_dict() == {"hp": 10}       # both sides untouched
    assert b.to_dict() == {"hp": 2}


def test_merge_accepts_plain_mappings():
    a = utils.gimdict({"hp": 10}, int="max")
    assert (a | {"hp": 7})["hp"] == 10
    assert a.merge({"hp": 30})["hp"] == 30


def test_iteration_is_key_sorted():
    # The flat engine's contract: items() is key-sorted, stated, not hidden.
    d = utils.gimdict({"c": 1, "a": 2, "b": 3})
    assert list(d) == ["a", "b", "c"]


def test_keys_must_be_str():
    with pytest.raises(TypeError):
        utils.gimdict({1: "x"})
    d = utils.gimdict()
    with pytest.raises(TypeError):
        d[1] = "x"


def test_engine_kwarg_flat_and_gated():
    d = utils.gimdict({"a": 1}, engine="flat")
    assert d["a"] == 1
    with pytest.raises(ValueError):
        utils.gimdict({"a": 1}, engine="hash")   # benchmark-gated, not silent


def test_merge_in_accumulates_in_place():
    d = utils.gimdict(int="sum")
    for sample in range(1, 101):
        d.merge_in("total", sample)
    assert d["total"] == 5050
    assert len(d) == 1
