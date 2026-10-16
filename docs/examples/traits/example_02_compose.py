# type: ignore
"""Compose a trait into a class: ``pygim.traits.compose`` and ``traits_of``.

A trait is a class whose methods are written to be copied into other classes; nothing inherits
from it. ``compose(Target, Trait)`` copies each member into ``Target``'s own namespace, rewritten
so it behaves as if it had been written there: its private names, its qualified name and its
``super()``. A name the target already has is refused unless ``override=True`` — and a refusal
writes nothing. ``traits_of`` records where each member came from (docs/design/traits.md).

This example demonstrates:
- a trait's functions, properties and classmethods composed into a class
- private names and super() working as if written in the class
- a conflict refused before anything is written, and override=True
- the record of where each composed member came from
"""

from pygim.traits import compose, traits_of


class Named:  # the trait: written once, composed wherever a name is kept privately
    def greeting(self):
        return f"hello {self.__name}"  # compiled as _Named__name; composed, it reads the target's

    @property
    def initials(self):
        return "".join(part[0] for part in self.__name.split())

    @classmethod
    def describe(cls):
        return f"{cls.__name__} has a name"


class Base:
    def title(self):
        return "base"


class Person(Base):
    def __init__(self, name):
        self.__name = name  # stored as _Person__name


# ----------------------------------------------------------------------------
# 1. Compose the trait: its members land in the class's own namespace
# ----------------------------------------------------------------------------
#              ┌─ target: the class that receives the members
#              │       ┌─ traits: one or more classes whose members are copied
#              ▼       ▼
assert compose(Person, Named) is Person

assert Person("Ada Lovelace").greeting() == "hello Ada Lovelace"
assert Person("Ada Lovelace").initials == "AL"
assert Person.describe() == "Person has a name"
assert "greeting" in vars(Person)                        # Person's own, not inherited
assert Person.greeting.__qualname__ == "Person.greeting"
assert "greeting" not in vars(Base)                      # the base is untouched


# ----------------------------------------------------------------------------
# 2. super() in a trait resolves in the class it was composed into
# ----------------------------------------------------------------------------
class Shouting:
    def title(self):
        return super().title().upper()


compose(Person, Shouting)

assert Person("Ada").title() == "BASE"                   # Person's base, not Shouting's


# ----------------------------------------------------------------------------
# 3. A conflict is refused, and nothing is written
# ----------------------------------------------------------------------------
class Louder:
    def greeting(self):
        return "HELLO"

    def wave(self):
        return "o/"


try:
    compose(Person, Louder)
except ValueError as error:
    assert "already has greeting" in str(error)
else:
    raise AssertionError("expected the conflict to be refused")

assert "wave" not in vars(Person)                        # checked before any member was written

#                       ┌─ override: replace the target's member instead of refusing
#                       ▼
compose(Person, Louder, override=True)

assert Person("Ada").greeting() == "HELLO"


# ----------------------------------------------------------------------------
# 4. The record: which trait, where it defined the member, where it was composed
# ----------------------------------------------------------------------------
record = traits_of(Person)

assert list(record) == ["describe", "greeting", "initials", "title", "wave"]
assert record["greeting"].trait == f"{__name__}.Louder"  # the override is recorded too
assert record["initials"].trait == f"{__name__}.Named"
assert len(traits_of(Base)) == 0

print("compose example OK:", record)
