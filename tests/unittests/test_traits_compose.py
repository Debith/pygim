# -*- coding: utf-8 -*-
"""``pygim.traits.compose`` and ``traits_of``: a trait's members copied into a class, rewritten to
belong there, and the record of where each came from.

A trait is a class whose members are written to be copied into other classes; nothing inherits
from it. ``compose(Target, Trait)`` copies each member into ``Target``'s own namespace, rewritten so
it behaves as if written there: its private names, its qualified name and its ``super()``. A name
the target already has is refused unless ``override=True``, and a refusal writes nothing.

The cases are ``traits_cases.COMPOSE``, ``COMPOSED_MEMBERS`` and ``TRAITS_OF``: one row per call,
argument columns then expected columns and effects (global #118).
"""

import re
import sys

import pytest

from pygim.traits import compose, traits_of
from traits_cases import (
    ABSENT,
    COMPOSE,
    COMPOSE_CALL,
    COMPOSED_MEMBERS,
    TARGET,
    TRAITS_OF,
    Fresh,
    Raises,
    Ref,
    defined_at,
)

def _target(cell):
    """(the target class, a factory of its instances) — or the raw value and None."""
    return cell.make() if isinstance(cell, Fresh) else (cell, None)


def _traits(case):
    return [case[c].resolve() if isinstance(case[c], Ref) else case[c]
            for c in ("traits[0]", "traits[1]") if case[c] is not ABSENT]


def _written(before, target):
    """The names the call set in the target's own namespace: new ones, and replaced ones."""
    return tuple(sorted(name for name, value in vars(target).items()
                        if name not in before or before[name] is not value))


def _read(instance, other, member):
    """`member` read on `instance`: `name()` calls it, `name(other)` calls it with `other`."""
    path, arguments = member, None
    if member.endswith("(other)"):
        path, arguments = member[: -len("(other)")], (other,)
    elif member.endswith("()"):
        path, arguments = member[:-2], ()
    value = instance
    for part in path.split("."):
        value = getattr(value, part)
    return value if arguments is None else value(*arguments)


# --- compose(): what it returns, writes and records -------------------------------------------


@pytest.mark.parametrize("case", COMPOSE.cases())
def test_compose(case):
    target, _ = _target(case["target"])
    before = dict(vars(target)) if isinstance(target, type) else {}
    expected = case["expected_result"]
    if isinstance(expected, Raises):
        with pytest.raises(expected.type, match=re.escape(expected.message) if expected.message else None):
            compose(target, *_traits(case), override=case["override"])
    else:
        assert expected is TARGET
        assert compose(target, *_traits(case), override=case["override"]) is target
    if case["effect: written"] is not ABSENT:
        assert _written(before, target) == case["effect: written"]
        assert tuple(traits_of(target)) == case["effect: traits_of"]


# --- the composed members, called -----------------------------------------------------------------


@pytest.mark.parametrize("case", COMPOSED_MEMBERS.cases())
def test_composed_member(case):
    target, make = _target(case["target"])
    compose(target, *_traits(case), override=case["override"])
    assert _read(make(), make(), case["member"]) == case["expected_result"]


# --- traits_of(): the record of one composed member ---------------------------------------------


@pytest.mark.parametrize("case", TRAITS_OF.cases())
def test_traits_of(case):
    target, _ = _target(case["target"])
    composed_at = f"{__file__}:{sys._getframe().f_lineno + 1}"
    compose(target, *_traits(case), override=case["override"])
    expected = case["expected_result.trait"]
    if isinstance(expected, Raises):
        with pytest.raises(expected.type):
            traits_of(target)[case["name"]]
        return
    record = traits_of(target)[case["name"]]
    assert record.trait == expected
    assert record.defined == defined_at(case["expected_result.defined"])
    assert case["expected_result.composed"] is COMPOSE_CALL
    assert record.composed == composed_at


# --- what no single row shows -----------------------------------------------------------------


def test_the_trait_itself_is_untouched():
    greet = Ref("Greeter.greet").resolve()
    target, _ = Fresh("Person").make()
    compose(target, Ref("Greeter").resolve())
    assert Ref("Greeter").resolve().__dict__["greet"] is greet
    assert vars(target)["greet"] is not greet
    assert "_Greeter__name" in greet.__code__.co_names


def test_a_composed_function_keeps_what_it_carries():
    """Everything a function carries besides its code comes along unchanged: its name (even one that
    differs from its code's), defaults, keyword defaults, annotations, docstring, module, attributes,
    type parameters and closure cells — only the code, the __class__ cell and the qualified name move."""
    captured = "from the enclosing scope"

    def greet(self, greeting="hi", *, punctuation="!") -> str:
        """Say hello."""
        return f"{greeting}{punctuation} {captured}"

    greet.__name__ = "renamed"            # differs from greet.__code__.co_name
    greet.__module__ = "elsewhere"
    greet.marker = "kept"
    if sys.version_info >= (3, 12):
        greet.__type_params__ = (type("T", (), {}),)
    trait = type("Trait", (), {"greet": greet})
    target = type("Target", (), {})
    compose(target, trait)

    composed = vars(target)["greet"]
    assert composed is not greet
    assert composed.__name__ == "renamed"
    assert composed.__defaults__ == ("hi",)
    assert composed.__kwdefaults__ == {"punctuation": "!"}
    assert composed.__annotations__ == {"return": str}
    assert composed.__doc__ == "Say hello."
    assert composed.__module__ == "elsewhere"
    assert composed.marker == "kept"
    assert composed.__globals__ is greet.__globals__
    if sys.version_info >= (3, 12):
        assert composed.__type_params__ == greet.__type_params__
    assert composed.__closure__[0] is greet.__closure__[0]   # an ordinary cell is shared, not copied
    assert target().greet() == "hi! from the enclosing scope"
    assert target().greet("hey", punctuation="?") == "hey? from the enclosing scope"
