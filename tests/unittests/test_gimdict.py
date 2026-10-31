from __future__ import annotations

import re
from collections.abc import ItemsView, KeysView, Mapping, MutableMapping, ValuesView
from types import MappingProxyType

import pytest

from pygim import utils

# What a MutableMapping is (its abstract methods and its mixins), as collections.abc says it; what
# the ABC itself carries to be one is left out.
CONTRACT = sorted({n for c in MutableMapping.__mro__[:-1] for n in vars(c)}
                  - {"__abstractmethods__", "__class_getitem__", "__doc__", "__module__", "__slots__",
                     "__subclasshook__", "_abc_impl", "_MutableMapping__marker"}
                  # what Python gives a class itself, not the contract: its source line and its
                  # attributes (3.13), its annotations (3.14)
                  - {"__firstlineno__", "__static_attributes__", "__annotate_func__", "__annotations_cache__"})


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


@pytest.mark.parametrize("strategy, left, right, kept", [
    ("max", 1, 1.0, 1), ("max", 1.0, 2, 2), ("min", 1, 1.0, 1), ("min", 2, 1.5, 1.5),
], ids=["max keeps the first of equals", "max", "min keeps the first of equals", "min"])
def test_max_and_min_merge_as_builtins_max_and_min(strategy, left, right, kept):
    """max and min keep what builtins.max(left, right) and min(left, right) keep — the first of two
    equals — compared in C, not by importing builtins on every merge (2026-10-29)."""
    d = utils.gimdict({"a": left})
    d.set_strategy("a", strategy)
    merged = (d | utils.gimdict({"a": right}))["a"]
    assert merged == kept and type(merged) is type(kept)


def test_max_of_values_that_have_no_order_raises_as_max_does():
    d = utils.gimdict({"a": 1})
    d.set_strategy("a", "max")
    with pytest.raises(TypeError):
        d | utils.gimdict({"a": "x"})


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
    assert (left | right).to_dict() == left.merge(right).to_dict() == {"a": 3}


def test_a_merge_leaves_both_sides_as_they_were():
    """`a | b` merged into a's own values — pybind11's py::dict(d) shares a dict, it does not copy
    it — so every merge changed its left side, and two merges in a row stacked (2026-10-30, found
    by the gimdict example). Its key strategies were shared too."""
    left, right = utils.gimdict({"a": 1, "n": {"x": 1}}, dict=utils.deep), utils.gimdict({"a": 2, "n": {"x": 2}})
    merged = left | right
    merged.set_strategy("a", "max")
    assert left.to_dict() == {"a": 1, "n": {"x": 1}} and right.to_dict() == {"a": 2, "n": {"x": 2}}
    assert (left | right)["a"] == 3                    # left's strategy for "a" is still int's, sum
    assert merged.to_dict() == {"a": 3, "n": {"x": 3}}


def test_to_dict_is_a_copy():
    d = utils.gimdict({"a": 1})
    d.to_dict()["a"] = 99
    assert d["a"] == 1


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


def test_every_method_of_the_contract_is_its_own():
    """gimdict registers as a MutableMapping, so it is one in full: until 2026-10-30 it had no keys,
    items, values, pop, popitem, clear, update or setdefault, and get(missing) raised (D66). The
    registration lends no method — each is gimdict's own."""
    assert [name for name in CONTRACT if name not in vars(utils.gimdict)] == []


def outcome(call, mapping):
    """What `call` gives on `mapping` — a view as its list, an error as its type and arguments, the
    mapping's own type named in them as <mapping> — and what is left in it after."""
    kind = type(mapping)
    named = re.compile(rf"'(?:{re.escape(kind.__module__)}\.)?{re.escape(kind.__name__)}'")   # 'dict', 'pygim.utils.gimdict'
    try:
        got = call(mapping)
        got = list(got) if isinstance(got, (KeysView, ValuesView, ItemsView)) else got
    except Exception as error:
        got = (type(error), tuple(named.sub("<mapping>", a) if isinstance(a, str) else a for a in error.args))
    return got, list(mapping.items())


# Every call of the contract, each where it succeeds and where it refuses: gimdict does what dict does.
CALLS = {
    "get": lambda m: m.get("a"), "get missing": lambda m: m.get("missing"),
    "get missing, a default": lambda m: m.get("missing", 42),
    "item missing": lambda m: m["missing"], "del missing": lambda m: m.__delitem__("missing"),
    "keys": lambda m: m.keys(), "values": lambda m: m.values(), "items": lambda m: m.items(),
    "pop": lambda m: m.pop("a"), "pop missing": lambda m: m.pop("missing"),
    "pop missing, a default": lambda m: m.pop("missing", 0), "pop, two defaults": lambda m: m.pop("a", 0, 1),
    "popitem": lambda m: m.popitem(), "popitem empty": lambda m: (m.clear(), m.popitem()),
    "clear": lambda m: m.clear(),
    "setdefault there": lambda m: m.setdefault("a", 9), "setdefault missing": lambda m: m.setdefault("z", 9),
    "setdefault missing, no default": lambda m: m.setdefault("z"),
    "setdefault unhashable": lambda m: m.setdefault([], 1),
    "update a mapping": lambda m: m.update({"a": 5, "c": 3}), "update pairs and keywords": lambda m: m.update([("c", 3)], d=4),
    "update keywords": lambda m: m.update(e=5), "update with a gimdict": lambda m: m.update(utils.gimdict({"a": 7})),
    "update, two mappings": lambda m: m.update({}, {}), "update, not pairs": lambda m: m.update([1]),
    "equal": lambda m: m == {"a": 1, "b": 2}, "equal, other order": lambda m: m == {"b": 2, "a": 1},
    "not equal": lambda m: m != {"a": 1}, "equal to a list": lambda m: m == [("a", 1), ("b", 2)],
    "equal to a proxy": lambda m: m == MappingProxyType({"a": 1, "b": 2}),
    "reversed": lambda m: list(reversed(m)), "hash": lambda m: hash(m),
    "len, in, iter": lambda m: (len(m), "a" in m, "z" in m, list(m)),
}


@pytest.mark.parametrize("call", CALLS.values(), ids=CALLS.keys())
def test_it_does_what_dict_does(call):
    assert outcome(call, utils.gimdict({"a": 1, "b": 2})) == outcome(call, {"a": 1, "b": 2})


def test_its_views_are_live_and_the_abc_s():
    d = utils.gimdict({"a": 1})
    keys, values, items = d.keys(), d.values(), d.items()
    assert isinstance(keys, KeysView) and isinstance(values, ValuesView) and isinstance(items, ItemsView)
    d["b"] = 2
    assert list(keys) == ["a", "b"] and list(values) == [1, 2] and ("b", 2) in items
    assert keys & {"b", "z"} == {"b"}


def test_it_equals_any_mapping_with_its_items_whatever_its_strategies():
    """As a Mapping's __eq__: the items, compared as dicts; the strategies are how values merge, not values."""
    d = utils.gimdict({"a": 1}, int=max)
    assert d == utils.gimdict({"a": 1}) and {"a": 1} == d and d != utils.gimdict({"a": 2})
    assert d == MappingProxyType({"a": 1}) and isinstance(d, Mapping)


def test_update_sets_as_setitem_does_and_merge_in_merges():
    """update is the contract's: d[key] = value for each. A strategy merges only through merge_in and |."""
    d = utils.gimdict({"a": 1})
    d.update({"a": 2})
    assert d["a"] == 2   # int's strategy is sum: an update that merged would give 3
    d.merge_in("a", 2)
    assert d["a"] == 4


def test_a_key_s_strategy_outlives_its_value():
    """clear, pop and del take the values; the strategies are the mapping's settings, kept."""
    d = utils.gimdict({"a": 1})
    d.set_strategy("a", "max")
    d.clear()
    d["a"] = 1
    assert (d | utils.gimdict({"a": 5}))["a"] == 5   # max: sum, int's own, would give 6


def test_it_is_built_from_and_into_any_mapping():
    d = utils.gimdict(utils.gimdict({"a": 1}))
    assert dict(d) == {"a": 1} and utils.gimdict(MappingProxyType({"b": 2})) == {"b": 2}


def test_its_repr_shows_its_items():
    assert repr(utils.gimdict({"a": 1})) == "gimdict({'a': 1})"
    d = utils.gimdict()
    d["self"] = d
    assert repr(d) == "gimdict({'self': gimdict({...})})"


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
