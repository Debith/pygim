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


# ── layers: merge with memory ────────────────────────────────────────────────

def test_layers_record_fold_and_remove():
    s = utils.gimdict({"speed": 30}, layers=True)
    s.apply("haste", "speed", 2, "multiply")
    s.apply("paralyzed", "speed", 0, "multiply")
    assert s["speed"] == 0                     # folded on read
    s.remove("paralyzed")                      # lift the condition
    assert s["speed"] == 60                    # old value returns, no inverses
    assert s.sources("speed") == ["haste"]


def test_layered_is_a_gimdict_family_member():
    s = utils.gimdict(layers=True)
    assert isinstance(s, utils.gimmap)
    assert isinstance(s, MutableMapping)


def test_layered_key_without_base_channel():
    s = utils.gimdict(layers=True)
    s.apply("blessing", "luck", 5)
    s.apply("blessing", "luck", 3)             # int type-default: sum
    assert s["luck"] == 8
    assert "luck" in s and len(s) == 1
    assert s.footprint("blessing") == ["luck"]


def test_snapshot_is_frozen_and_independent():
    s = utils.gimdict({"hp": 10}, layers=True)
    s.merge_from({"hp": 2}, source="race")
    snap = s.snapshot()
    assert isinstance(snap, utils.frozen_gimmap)
    assert snap["hp"] == 12
    s.apply("curse", "hp", -100)               # later edit
    assert snap["hp"] == 12 and s["hp"] == -88


def test_layered_merge_with_source_is_removable():
    base = utils.gimdict({"hp": 10, "speed": 30}, layers=True)
    grown = base.merge({"hp": 2}, source="race")
    assert grown["hp"] == 12
    assert base["hp"] == 10                    # merge(source=...) is functional
    grown.remove("race")
    assert grown["hp"] == 10


def test_layered_functional_merge_returns_frozen():
    s = utils.gimdict({"hp": 10}, layers=True)
    s.apply("race", "hp", 2)
    folded = s | {"hp": 5}
    assert isinstance(folded, utils.frozen_gimmap)
    assert folded["hp"] == 17                  # observed 12, then +5


def test_frozen_and_layers_are_exclusive():
    with pytest.raises(ValueError):
        utils.gimdict({}, frozen=True, layers=True)


def test_plain_gimdict_has_no_layer_surface():
    d = utils.gimdict({})
    assert not hasattr(d, "apply")
    assert not hasattr(d, "sources")
    assert not hasattr(d, "snapshot")


# ── review regressions: every finding stays fixed ────────────────────────────

def test_copy_construct_from_every_family_member():
    d1 = utils.gimdict({"hp": 10, "ab": 1})
    assert utils.gimdict(d1).to_dict() == {"hp": 10, "ab": 1}
    f = utils.gimdict({"ab": 2}, frozen=True)
    assert utils.gimdict(f).to_dict() == {"ab": 2}
    s = utils.gimdict({"hp": 10}, layers=True)
    s.apply("race", "hp", 2)
    assert utils.gimdict(s).to_dict() == {"hp": 12}     # observed values travel


def test_dict_conversion_and_mapping_views():
    d = utils.gimdict({"b": 2, "a": 1})
    assert dict(d) == {"a": 1, "b": 2}
    assert list(d.keys()) == ["a", "b"]
    assert list(d.values()) == [1, 2]
    assert list(d.items()) == [("a", 1), ("b", 2)]
    assert dict(d.freeze()) == {"a": 1, "b": 2}


def test_equality_across_family_types():
    f = utils.gimdict({"ab": 1}, frozen=True)
    assert f == utils.gimdict({"ab": 1})
    assert f == {"ab": 1}
    assert not (f == utils.gimdict({"ab": 2}))


def test_chained_merge_folds_left():
    a = utils.gimdict({"hp": 1}, int="sum")
    total = a | utils.gimdict({"hp": 2}) | {"hp": 3}
    assert isinstance(total, utils.frozen_gimmap)
    assert total["hp"] == 6


def test_strategies_survive_freeze_thaw_and_chained_merges():
    # The character.py assembly pattern: fold-left with deep/union stacking.
    acc = utils.gimdict({}, dict=utils.deep, list=utils.union)
    acc = acc | {"abilities": {"DEX": 3}, "gear": ["Dagger"]}
    acc = acc | {"abilities": {"DEX": 1, "WIS": 2}, "gear": ["Dagger", "Rope"]}
    assert acc.to_dict() == {"abilities": {"DEX": 4, "WIS": 2},
                             "gear": ["Dagger", "Rope"]}
    thawed = acc.thaw()                              # configuration restored
    folded = thawed | {"abilities": {"CON": 1}}
    assert folded["abilities"] == {"DEX": 4, "WIS": 2, "CON": 1}


def test_ror_lets_other_operands_lead():
    d = utils.gimdict({"hp": 2}, int="sum")
    out = {"hp": 10} | d          # dict.__or__ -> NotImplemented -> d.__ror__
    assert out["hp"] == 12

    class Wrapper:
        def __ror__(self, other):
            return "wrapped"

    assert (utils.gimdict({"a": 1}) | Wrapper()) == "wrapped"


def test_reentrant_combine_is_safe():
    d = utils.gimdict(int="sum")

    class Evil(int):
        def __radd__(self, other):
            for i in range(64):                      # reallocate storage mid-combine
                d[f"x{i}"] = i
            return int(other) + int(self)

    d["k"] = 1
    d.merge_in("k", Evil(5))
    assert d["k"] == 6                               # lands in the right slot
    assert len(d) == 65


def test_non_str_keys_raise_on_the_whole_surface():
    d = utils.gimdict({"a": 1})
    with pytest.raises(TypeError):
        1 in d                                       # `in` included — no silent False
    with pytest.raises(TypeError):
        d[1]
    with pytest.raises(TypeError):
        1 in d.freeze()


def test_get_quirk_is_family_consistent():
    d = utils.gimdict({"a": 1})
    f = d.freeze()
    with pytest.raises(KeyError):
        d.get("missing")
    with pytest.raises(KeyError):
        f.get("missing")                             # same quirk on both sides
    assert f.get("missing", 42) == 42
