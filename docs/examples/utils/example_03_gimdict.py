# type: ignore
"""A mapping whose values merge by strategy: ``pygim.utils.gimdict``.

Combining two configurations, two character sheets or two sets of counts is
the same question asked of every key both have: replace the old value, add to
it, keep the larger, append to it? A gimdict answers it once — per key, per
type of value, or for everything — and ``a | b`` (or ``merge_in``) applies the
answer. Everything else is a dict: gimdict is a ``MutableMapping`` in full.

This example demonstrates:
- The default strategies: numbers add up, anything else is replaced
- A strategy per type of value and per key, the key's winning
- deep: nested mappings merged key by key, and union: lists without repeats
- The mapping itself: get, setdefault, update, pop, views, ==, as a dict does
"""

from collections.abc import MutableMapping

from pygim import utils

# ----------------------------------------------------------------------------
# 1. The defaults: numbers add up, anything else is replaced
# ----------------------------------------------------------------------------
monday = utils.gimdict({"visits": 3, "last_page": "/home"})
tuesday = utils.gimdict({"visits": 5, "last_page": "/docs"})
week = monday | tuesday                      # a new gimdict; neither side changes
assert week["visits"] == 8                   # int: sum
assert week["last_page"] == "/docs"          # str: replace
assert monday["visits"] == 3

# ----------------------------------------------------------------------------
# 2. A strategy per type of value, and per key
# ----------------------------------------------------------------------------
#                                                ┌─ int: how ints merge (max, min, sum, or a name)
#                                                │
#                                                ▼
scores = utils.gimdict({"best": 70, "tries": 1}, int=max)
scores.set_strategy("tries", "sum")          # a key's strategy comes before its type's
scores.merge_in("best", 64)                  # merged into what is there, by that strategy
scores.merge_in("tries", 1)
assert scores["best"] == 70                  # max
assert scores["tries"] == 2                  # sum, the key's own
assert scores.type_strategy("int") == "max"

# ----------------------------------------------------------------------------
# 3. deep and union: containers merged rather than replaced
# ----------------------------------------------------------------------------
sheet = utils.gimdict({"abilities": {"DEX": 3, "STR": 0}, "gear": ["Dagger", "Rope"]},
                      dict=utils.deep, list=utils.union)
feat = utils.gimdict({"abilities": {"DEX": 1, "WIS": 2}, "gear": ["Dagger", "Lantern"]})
merged = sheet | feat
# deep: key by key, each leaf by its own strategy (DEX's ints add up)
assert merged["abilities"] == {"DEX": 4, "STR": 0, "WIS": 2}
# union: what was there, then what is new
assert merged["gear"] == ["Dagger", "Rope", "Lantern"]

# ----------------------------------------------------------------------------
# 4. Everything else is a dict's
# ----------------------------------------------------------------------------
d = utils.gimdict({"a": 1})
assert isinstance(d, MutableMapping)
assert d.get("missing") is None          # a Mapping's get: no KeyError
assert d.get("missing", 0) == 0
assert d.setdefault("b", 2) == 2
assert d["b"] == 2
d.update({"a": 10})                          # update sets, as d[key] = value does: no strategy
assert d["a"] == 10
keys = d.keys()                              # a live view, as a dict's
d["c"] = 3
assert list(keys) == ["a", "b", "c"]
assert d.pop("c") == 3
assert d.pop("c", None) is None
assert d == {"a": 10, "b": 2}                # equal to any mapping of the same items
assert dict(d) == {"a": 10, "b": 2}

print(f"gimdict example OK: {week['visits']} visits, best {scores['best']}, gear {merged['gear']}")
