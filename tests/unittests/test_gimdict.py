from __future__ import annotations

from collections.abc import MutableMapping

import pytest

from pygim import utils


def test_gimdict_is_mutable_mapping():
    d = utils.gimdict({"a": 1}, int=max, str=utils.replace)
    assert isinstance(d, MutableMapping)


def test_gimdict_constructor_with_type_strategies():
    d1 = utils.gimdict({"a": 1, "b": "2"}, int=max, str=utils.replace)
    d2 = utils.gimdict({"a": 5, "b": "7"})
    d3 = d1 | d2

    assert d3["a"] == 5
    assert d3["b"] == "7"


def test_gimdict_default_per_type_rules():
    d1 = utils.gimdict({"a": 1, "b": "x"})
    d2 = utils.gimdict({"a": 2, "b": "y"})
    d3 = d1 | d2
    assert d3["a"] == 3
    assert d3["b"] == "y"


def test_gimdict_per_key_strategy_overrides_type_strategy():
    d1 = utils.gimdict({"x": 9}, int=min)
    d1.set_strategy("x", "max")
    d2 = utils.gimdict({"x": 3})
    d3 = d1 | d2
    assert d3["x"] == 9


def test_gimdict_mapping_protocol_set_get_delete_iter_len_contains():
    d = utils.gimdict({"a": 1})
    d["b"] = 2
    assert d["a"] == 1
    assert d.get("b") == 2
    assert d.get("missing", 42) == 42
    assert "a" in d
    assert len(d) == 2
    assert set(iter(d)) == {"a", "b"}
    del d["a"]
    assert "a" not in d


def test_gimdict_merge_method_matches_or_operator():
    left = utils.gimdict({"a": 1})
    right = utils.gimdict({"a": 2})
    assert (left | right).to_dict() == left.merge(right).to_dict()


@pytest.mark.parametrize("strategy", ["sum", "max", "min", "replace", max, min, sum])
def test_gimdict_accepts_strategy_aliases(strategy):
    d = utils.gimdict()
    d.set_default_strategy(strategy)


@pytest.mark.parametrize("strategy", ["", "AVG", "unknown", object()])
def test_gimdict_rejects_invalid_strategies(strategy):
    d = utils.gimdict()
    with pytest.raises(ValueError):
        d.set_default_strategy(strategy)
    with pytest.raises(ValueError):
        d.set_strategy("a", strategy)
    with pytest.raises(ValueError):
        d.set_type_strategy("int", strategy)
    with pytest.raises(ValueError):
        utils.gimdict({}, int=strategy)


def test_gimdict_rejects_non_mapping_initializer():
    with pytest.raises(TypeError):
        utils.gimdict(123)


def test_gimdict_get_missing_without_default_raises_key_error():
    d = utils.gimdict()
    with pytest.raises(KeyError):
        _ = d.get("missing")


def test_gimdict_conflict_error_for_invalid_sum_operation():
    # Force sum for strings, then make conflict between str and int.
    left = utils.gimdict({"k": "x"}, str="sum")
    right = utils.gimdict({"k": 1})
    with pytest.raises(TypeError):
        _ = left | right


def test_gimdict_deep_strategy_merges_nested_mappings_recursively():
    sheet = utils.gimdict({"abilities": {"DEX": 3, "STR": 0}}, dict=utils.deep)
    feat = utils.gimdict({"abilities": {"DEX": 1, "WIS": 2}})
    merged = (sheet | feat).to_dict()
    assert merged["abilities"] == {"DEX": 4, "STR": 0, "WIS": 2}   # leaves stack


def test_gimdict_deep_strategy_recurses_to_any_depth():
    d1 = utils.gimdict({"a": {"b": {"c": 1}}}, dict=utils.deep)
    d2 = utils.gimdict({"a": {"b": {"c": 2, "d": 9}}})
    assert (d1 | d2).to_dict() == {"a": {"b": {"c": 3, "d": 9}}}


def test_gimdict_deep_falls_back_to_replace_for_non_mappings():
    d1 = utils.gimdict({"a": {"b": 1}}, dict=utils.deep)
    d2 = utils.gimdict({"a": "flat"})
    assert (d1 | d2).to_dict() == {"a": "flat"}


def test_gimdict_extend_concatenates_sequences():
    d1 = utils.gimdict({"w": ["a"]}, list=utils.extend)
    d2 = utils.gimdict({"w": ["b", "a"]})
    assert (d1 | d2).to_dict() == {"w": ["a", "b", "a"]}


def test_gimdict_union_appends_only_unseen_elements():
    d1 = utils.gimdict({"gear": ["Dagger", "Rope"]}, list=utils.union)
    d2 = utils.gimdict({"gear": ["Dagger", "Lantern"]})
    assert (d1 | d2).to_dict() == {"gear": ["Dagger", "Rope", "Lantern"]}


def test_gimdict_union_on_non_lists_replaces():
    d1 = utils.gimdict({"x": "a"})
    d1.set_strategy("x", "union")
    d2 = utils.gimdict({"x": "b"})
    assert (d1 | d2).to_dict() == {"x": "b"}


@pytest.mark.parametrize("name", ["deep", "extend", "union"])
def test_gimdict_new_strategy_aliases_round_trip(name):
    d = utils.gimdict({})
    d.set_type_strategy("list", name)
    assert d.type_strategy("list") == name
