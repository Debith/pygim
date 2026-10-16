# -*- coding: utf-8 -*-
"""``pygim.traits.compose``: a trait's members copied into a class, rewritten to belong there.

A trait is a class whose members are written to be copied into other classes; nothing inherits
from it. ``compose(Target, Trait)`` copies each member into ``Target``'s own namespace, rewritten so
it behaves as if written there: its private names, its qualified name and its ``super()``. A name
the target already has is refused unless ``override=True``, and a refusal writes nothing.
``traits_of(Target)`` says where each composed member came from.
"""

import dataclasses
import enum
import inspect

import pytest

from pygim.traits import compose, traits_of


class Greeter:
    def greet(self):
        return f"hello {self.__name}"

    def shout(self):
        return (lambda: self.__name.upper())()

    @property
    def name_length(self):
        return len(self.__name)

    @classmethod
    def kind(cls):
        return cls.__name__

    @staticmethod
    def version():
        return 1


def _person():
    """A fresh class to compose into, so no test sees another's."""

    class Person:
        def __init__(self, name):
            self.__name = name  # stored as _Person__name

    return Person


# --- each kind of member ------------------------------------------------------------------------


def test_compose_returns_the_target():
    person = _person()
    assert compose(person, Greeter) is person


def test_a_function_reads_the_targets_private_attribute():
    person = compose(_person(), Greeter)
    assert person("ann").greet() == "hello ann"


def test_a_function_with_nested_code_reads_it_too():
    person = compose(_person(), Greeter)
    assert person("ann").shout() == "ANN"


def test_a_property_is_composed():
    person = compose(_person(), Greeter)
    assert person("ann").name_length == 3


def test_a_classmethod_receives_the_target():
    person = compose(_person(), Greeter)
    assert person.kind() == "Person"


def test_a_staticmethod_is_composed():
    person = compose(_person(), Greeter)
    assert person.version() == 1


# --- composed members belong to the target --------------------------------------------------------


def test_composed_members_live_in_the_targets_own_namespace():
    person = compose(_person(), Greeter)
    assert {"greet", "shout", "name_length", "kind", "version"} <= set(vars(person))


def test_a_composed_function_carries_the_targets_qualified_name():
    person = compose(_person(), Greeter)
    assert person.greet.__qualname__ == f"{person.__qualname__}.greet"


def test_the_trait_itself_is_untouched():
    before = Greeter.__dict__["greet"]
    person = compose(_person(), Greeter)
    assert Greeter.__dict__["greet"] is before
    assert person.__dict__["greet"] is not before
    assert "_Greeter__name" in before.__code__.co_names


def test_super_in_a_trait_reaches_the_targets_base():
    class Base:
        def hello(self):
            return "base"

    class Polite:
        def hello(self):
            return "polite+" + super().hello()

    class Child(Base):
        pass

    compose(Child, Polite)
    assert Child().hello() == "polite+base"


def test_an_inherited_name_is_not_a_conflict():
    class Ordered:
        def __lt__(self, other):  # every class inherits object.__lt__
            return True

    target = compose(_person(), Ordered)
    assert target("a") < target("b")


def test_a_dataclass_takes_a_trait():
    @dataclasses.dataclass(frozen=True, slots=True)
    class Point:
        x: int

    class Doubled:
        def doubled(self):
            return self.x * 2

    assert compose(Point, Doubled)(4).doubled() == 8


def test_an_enum_takes_a_trait():
    class Colour(enum.Enum):
        RED = "red"

    class Shouted:
        def shouted(self):
            return self.value.upper()

    assert compose(Colour, Shouted).RED.shouted() == "RED"


# --- conflicts ------------------------------------------------------------------------------------


def test_a_name_the_target_has_is_refused_and_nothing_is_written():
    person = _person()
    person.shout = lambda self: "mine"
    with pytest.raises(ValueError, match="already has shout"):
        compose(person, Greeter)
    assert "greet" not in vars(person)  # checked before any member was written
    assert person("ann").shout() == "mine"


def test_override_replaces_the_targets_member():
    person = _person()
    person.shout = lambda self: "mine"
    compose(person, Greeter, override=True)
    assert person("ann").shout() == "ANN"


def test_two_traits_defining_one_name_are_refused():
    class Loud:
        def shout(self):
            return "LOUD"

    person = _person()
    with pytest.raises(ValueError, match="both define shout"):
        compose(person, Greeter, Loud)
    assert "greet" not in vars(person)


def test_with_override_the_later_trait_wins():
    class Loud:
        def shout(self):
            return "LOUD"

    person = compose(_person(), Greeter, Loud, override=True)
    assert person("ann").shout() == "LOUD"


# --- what a trait may hold --------------------------------------------------------------------------


def test_a_constant_in_a_trait_is_refused_by_name():
    class WithConstant:
        factor = 2

        def scaled(self):
            return self.factor

    person = _person()
    with pytest.raises(TypeError, match="WithConstant.factor is int"):
        compose(person, WithConstant)
    assert "scaled" not in vars(person)


def test_a_trait_that_is_not_a_class_is_refused():
    with pytest.raises(TypeError, match="compose takes classes as traits, not function"):
        compose(_person(), lambda self: None)


# --- where a member came from -----------------------------------------------------------------------


def test_traits_of_a_class_never_composed_is_empty():
    assert len(traits_of(_person())) == 0


def test_traits_of_names_each_composed_member_in_order():
    person = compose(_person(), Greeter)
    assert list(traits_of(person)) == ["greet", "kind", "name_length", "shout", "version"]


def test_traits_of_says_which_trait_where_defined_and_where_composed():
    person = _person()
    line = inspect.currentframe().f_lineno + 1
    compose(person, Greeter)
    record = traits_of(person)["greet"]
    assert record.trait == f"{__name__}.Greeter"
    assert record.defined == f"{__file__}:{Greeter.greet.__code__.co_firstlineno}"
    assert record.composed == f"{__file__}:{line}"


def test_traits_of_an_unknown_member_is_a_key_error():
    with pytest.raises(KeyError):
        traits_of(compose(_person(), Greeter))["missing"]
