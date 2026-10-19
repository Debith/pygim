# -*- coding: utf-8 -*-
"""The record of what compose() did — `traits_of(cls)` — kept off the class itself.

Decided 2026-10-19 (R1–R3 in docs/design/traits.md, from the PR #38 review): CPython gives a class
no private slot, so for any class compose() accepts, the record lives in a side table, keyed by the
class's identity and evicted when the class is collected — functools.singledispatch's pattern, with
identity in place of equality, so no metaclass `__eq__`/`__hash__` is asked (R1). A class made by
`gim_type` will carry its record as a member instead. Composing a class that has a record carries
that record along, attributed to where each member came from (R2), and nothing named like the old
`__pygim_traits__` attribute is special any more (R3).
"""

import gc
import sys

from pygim.traits import compose, traits_of

MODULE = __name__


def _function(value):
    def member(self):
        return value
    return member


def _line():
    """`file:line` of the caller's next line — where the compose() call below it sits."""
    frame = sys._getframe(1)
    return f"{frame.f_code.co_filename}:{frame.f_lineno + 1}"


class TestTheRecordIsKeptOffTheClass:
    def test_compose_writes_only_the_members(self):
        target = type("Target", (), {})
        before = set(vars(target))
        compose(target, type("Trait", (), {"greet": _function("hi"), "wave": _function("bye")}))
        assert set(vars(target)) - before == {"greet", "wave"}            # no bookkeeping attribute
        assert list(traits_of(target)) == ["greet", "wave"]

    def test_records_stay_apart_even_when_a_metaclass_calls_every_class_equal(self):
        """A weak-keyed dict would ask the class for __hash__ and __eq__; identity asks nothing."""
        asked = []

        class Liar(type):
            def __eq__(cls, other):
                asked.append("__eq__")
                return True

            def __hash__(cls):
                asked.append("__hash__")
                return 1

        first = Liar("First", (), {})
        second = Liar("Second", (), {})
        compose(first, type("Trait", (), {"greet": _function("hi")}))
        compose(second, type("Trait", (), {"wave": _function("bye")}))
        assert list(traits_of(first)) == ["greet"]
        assert list(traits_of(second)) == ["wave"]
        assert asked == []

    def test_a_dead_classs_record_never_reaches_a_new_class_at_its_address(self):
        """CPython hands a freed class's memory to the next class almost every time (200 of 200 when
        measured), so a record that outlived its class would be read as the new class's own."""
        seen_reuse = False
        trait = type("Trait", (), {"greet": _function("hi")})   # made once: only `old` is freed below
        for _ in range(50):
            old = type("Old", (), {})
            compose(old, trait)
            address = id(old)
            del old
            gc.collect()
            new = type("New", (), {})
            seen_reuse |= id(new) == address
            assert list(traits_of(new)) == []
            del new
            gc.collect()
        assert seen_reuse, "no address was reused, so this test showed nothing"


class TestComposingAComposedClass:
    """R2: `class1 << class2`, where class2 had traits composed into it, gives class1 the whole story."""

    def test_a_member_is_attributed_to_the_trait_it_first_came_from(self):
        trait = type("First", (), {"greet": _function("hi"), "__module__": MODULE})
        middle = type("Middle", (), {"__module__": MODULE})
        compose(middle, trait)
        target = type("Target", (), {})
        at = _line()
        compose(target, middle)
        record = traits_of(target)["greet"]
        assert record.trait == f"{MODULE}.First"
        assert record.defined == traits_of(middle)["greet"].defined          # where the trait defined it
        assert record.composed == at                                         # where target got it

    def test_a_composed_classs_own_member_is_attributed_to_that_class(self):
        trait = type("First", (), {"greet": _function("hi"), "__module__": MODULE})
        middle = type("Middle", (), {"own": _function("mine"), "__module__": MODULE})
        compose(middle, trait)
        target = type("Target", (), {})
        compose(target, middle)
        assert traits_of(target)["own"].trait == f"{MODULE}.Middle"
        assert traits_of(target)["greet"].trait == f"{MODULE}.First"
        assert target().own() == "mine" and target().greet() == "hi"


class TestTheRecordIsAccurate:
    """D15 (Debith, 2026-10-19: "A, accurately"): traits_of(cls) lists only the members cls still holds
    as compose() left them. A member replaced or deleted since is the class's own business, and
    composing the class elsewhere attributes it to the class, not to the trait it once came from."""

    def _middle(self):
        trait = type("Greeter", (), {"greet": _function("hello"), "__module__": MODULE})
        middle = type("Middle", (), {"__module__": MODULE})
        compose(middle, trait)
        return middle, trait

    def test_a_replaced_member_is_no_longer_reported(self):
        middle, _ = self._middle()
        middle.greet = _function("custom")
        assert list(traits_of(middle)) == []

    def test_a_deleted_member_is_no_longer_reported(self):
        middle, _ = self._middle()
        del middle.greet
        assert list(traits_of(middle)) == []

    def test_an_unrelated_change_leaves_the_record_alone(self):
        middle, _ = self._middle()
        middle.other = _function("other")
        assert list(traits_of(middle)) == ["greet"]

    def test_a_replaced_member_is_attributed_to_the_class_that_holds_it(self):
        middle, _ = self._middle()

        def custom(self):
            return "custom"

        middle.greet = custom
        target = type("Target", (), {})
        compose(target, middle)
        record = traits_of(target)["greet"]
        assert record.trait == f"{MODULE}.Middle"
        assert record.defined == f"{custom.__code__.co_filename}:{custom.__code__.co_firstlineno}"
        assert target().greet() == "custom"

    def test_composing_over_a_replaced_member_reports_it_again(self):
        middle, trait = self._middle()
        middle.greet = _function("custom")
        compose(middle, trait, override=True)
        assert traits_of(middle)["greet"].trait == f"{MODULE}.Greeter"

    def test_a_member_a_hook_stored_differently_is_still_reported(self):
        """A metaclass __setattr__ may store something other than what compose() passed; the record
        holds what landed in the class, so the member is still compose()'s."""
        class Wrapping(type):
            def __setattr__(cls, name, value):
                super().__setattr__(name, staticmethod(value) if callable(value) else value)

        target = Wrapping("Target", (), {})
        compose(target, type("Greeter", (), {"greet": _function("hello")}))
        assert isinstance(vars(target)["greet"], staticmethod)
        assert list(traits_of(target)) == ["greet"]
