# -*- coding: utf-8 -*-
"""``pygim.traits.CodeBuilder``: a code object rebuilt so it belongs to another class.

Python compiles a private name (``self.__secret``) inside a class as ``_Class__secret``. A method's
code copied into another class keeps the first class's spelling, so it reads an attribute the new
class never stored. The builder rewrites those names — and the qualified name — for the new owner,
in the code and in every code object nested in it.
"""

import sys
import types

import pytest

from pygim.traits import CodeBuilder


class Trait:
    def plain(self):
        return self.__secret

    def nested(self):
        return (lambda: self.__secret)()

    def public(self):
        return self.value

    def dunder(self):
        return self.__class__.__name__


def _target():
    """A fresh class to move methods into, so no test sees another's."""

    class Target:
        def __init__(self):
            self.__secret = 42  # stored as _Target__secret
            self.value = 7

    return Target


def _move(source, name, target):
    """*source*'s method *name*, rebuilt for *target* and set on it; the bound method of an instance."""
    func = source.__dict__[name]
    code = CodeBuilder(func.__code__).reown(source, target).build()
    setattr(target, name, types.FunctionType(code, func.__globals__, name, func.__defaults__, func.__closure__))
    return getattr(target(), name)


def _nested_codes(code):
    return [c for c in code.co_consts if isinstance(c, types.CodeType)]


# --- private names ----------------------------------------------------------------------------


def test_a_private_name_is_spelt_for_the_new_owner():
    code = CodeBuilder(Trait.plain.__code__).reown(Trait, _target()).build()
    assert "_Target__secret" in code.co_names
    assert "_Trait__secret" not in code.co_names


def test_the_moved_method_reads_the_new_owners_private_attribute():
    assert _move(Trait, "plain", _target())() == 42


def test_a_nested_code_object_is_rebuilt_too():
    assert _move(Trait, "nested", _target())() == 42


def test_a_public_name_is_left_as_it_is():
    code = CodeBuilder(Trait.public.__code__).reown(Trait, _target()).build()
    assert code.co_names == Trait.public.__code__.co_names
    assert _move(Trait, "public", _target())() == 7


def test_a_dunder_name_is_left_as_it_is():
    code = CodeBuilder(Trait.dunder.__code__).reown(Trait, _target()).build()
    assert code.co_names == Trait.dunder.__code__.co_names
    assert _move(Trait, "dunder", _target())() == "Target"


# --- the owner's spelling: Python's mangling rule ----------------------------------------------


def test_leading_underscores_of_a_class_name_are_dropped():
    class _Trait:
        def plain(self):
            return self.__secret  # compiled as _Trait__secret

    class __Target:
        def __init__(self):
            self.__secret = 42  # stored as _Target__secret

    assert _move(_Trait, "plain", __Target)() == 42


def test_a_class_named_only_with_underscores_mangles_nothing():
    class _:
        def plain(self):
            return self.__secret  # not mangled: the class name is all underscores

    assert "__secret" in _.plain.__code__.co_names
    assert _move(_, "plain", _target())() == 42


def test_reown_steps_apply_in_order():
    first = _target()

    class Second:
        def __init__(self):
            self.__secret = 42

    code = CodeBuilder(Trait.plain.__code__).reown(Trait, first).reown(first, Second).build()
    assert "_Second__secret" in code.co_names


# --- the qualified name -------------------------------------------------------------------------


@pytest.mark.skipif(sys.version_info < (3, 11), reason="a code object has co_qualname from Python 3.11")
def test_the_qualified_name_moves_to_the_new_owner():
    target = _target()
    code = CodeBuilder(Trait.nested.__code__).reown(Trait, target).build()
    assert code.co_qualname == f"{target.__qualname__}.nested"
    (inner,) = _nested_codes(code)
    assert inner.co_qualname == f"{target.__qualname__}.nested.<locals>.<lambda>"


# --- a builder builds; the original stays ---------------------------------------------------------


def test_the_original_code_object_is_untouched():
    before = Trait.plain.__code__
    CodeBuilder(before).reown(Trait, _target()).build()
    assert Trait.plain.__code__ is before
    assert "_Trait__secret" in before.co_names


def test_building_twice_gives_equal_code():
    builder = CodeBuilder(Trait.nested.__code__).reown(Trait, _target())
    assert builder.build() == builder.build()


def test_a_builder_with_no_steps_builds_an_equal_copy():
    assert CodeBuilder(Trait.nested.__code__).build() == Trait.nested.__code__


# --- what it refuses ------------------------------------------------------------------------------


def test_something_that_is_not_code_is_refused_by_name():
    with pytest.raises(TypeError, match="CodeBuilder takes a code object, not str"):
        CodeBuilder("return 1")


def test_an_owner_that_is_not_a_class_is_refused():
    with pytest.raises(TypeError):
        CodeBuilder(Trait.plain.__code__).reown(Trait, "Target")
