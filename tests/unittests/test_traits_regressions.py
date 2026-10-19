# -*- coding: utf-8 -*-
"""Authored regression seeds: one case per defect the adversarial work found in compose() and
CodeBuilder, each written before its fix and failing on the code before it. The generated
adversarial spaces (traits_adversarial.py) cover the families these belong to; these name the cases
that were seen. A case that crashed the process runs in a bounded child (bounded_child.py).
"""

import pytest

import bounded_child
from pygim.traits import CodeBuilder, compose, traits_of


def _function(name):
    def member(self):
        return name
    return member


class TestTheNameWrittenIsTheNameChecked:
    def test_a_member_name_with_a_nul_is_written_whole(self):
        """F1: the write used the name up to its NUL — "greet\\0x" was checked, "greet" overwritten."""

        class Person:
            def greet(self):
                return "own"

        own = Person.__dict__["greet"]
        compose(Person, type("Trait", (), {"greet\x00x": _function("trait")}))
        assert Person.__dict__["greet"] is own
        assert "greet\x00x" in Person.__dict__
        assert list(traits_of(Person)) == ["greet\x00x"]

    def test_a_str_subclass_key_is_composed_under_its_own_characters(self):
        """F4: the name came from the key's __str__, which a str subclass can override."""

        class Lying(str):
            def __str__(self):
                raise AssertionError("compose() must not ask a key for its __str__")

        class Person:
            pass

        compose(Person, type("Trait", (), {Lying("greet"): _function("trait")}))
        assert "greet" in Person.__dict__
        assert list(traits_of(Person)) == ["greet"]


class TestTheRecordSurvivesWhatTheWritesDo:
    """F2: when the record was the class attribute `__pygim_traits__`, a member of that name replaced
    it mid-write while compose() held a reference into it — a use after free. Fixed first by refusing
    the name; since 2026-10-19 (R1, R3 in traits.md) the record is kept off the class, so the name is
    an ordinary one and nothing a write does can reach the record. Still run in a bounded child: the
    crash it was."""

    def test_a_trait_member_named_like_the_old_record_is_an_ordinary_member(self):
        outcome = bounded_child.run("""
            from pygim.traits import compose, traits_of
            class Target: pass
            Trait = type("Trait", (), {"a": lambda self: 1, "__pygim_traits__": lambda self: 2})
            compose(Target, Trait)
            print("written:", sorted(k for k in vars(Target) if k in ("a", "__pygim_traits__")))
            print("record:", list(traits_of(Target)))
        """)
        assert not outcome.crashed and not outcome.timed_out, outcome.stderr
        assert outcome.stdout.splitlines() == ["written: ['__pygim_traits__', 'a']",
                                               "record: ['__pygim_traits__', 'a']"]

    def test_a_hook_tampering_with_the_class_mid_write_cannot_reach_the_record(self):
        outcome = bounded_child.run("""
            from pygim.traits import compose, traits_of
            class Meta(type):
                def __setattr__(cls, name, value):
                    super().__setattr__(name, value)
                    if name == "b":
                        for attr in [k for k in cls.__dict__ if k.startswith("__pygim")]:
                            delattr(cls, attr)  # the hook's own doing, not compose()'s
            class Target(metaclass=Meta): pass
            compose(Target, type("Trait", (), {"a": lambda self: 1, "b": lambda self: 2, "c": lambda self: 3}))
            print("written:", sorted(k for k in vars(Target) if k in "abc"))
            print("record:", list(traits_of(Target)))
        """)
        assert not outcome.crashed and not outcome.timed_out, outcome.stderr
        assert outcome.stdout.splitlines() == ["written: ['a', 'b', 'c']", "record: ['a', 'b', 'c']"]


def test_a_code_object_nested_too_deep_raises_instead_of_crashing():
    """F3: build() recursed natively once per nesting level, with no limit: a deep tree crashed."""
    outcome = bounded_child.run("""
        from pygim.traits import CodeBuilder
        code = compile("x", "<deep>", "eval")
        for _ in range(200_000):
            code = code.replace(co_consts=(code,))
        try:
            CodeBuilder(code).reown(type("A", (), {}), type("B", (), {})).build()
        except RecursionError:
            print("RecursionError")
    """, timeout=60)
    assert not outcome.crashed and not outcome.timed_out, outcome.stderr
    assert outcome.stdout.strip() == "RecursionError"


def test_code_nested_as_deep_as_the_compiler_allows_still_builds():
    source = "x"
    for _ in range(90):  # nested lambdas, as written in source; the parser's own limit is lower than the guard's
        source = f"(lambda: {source})"
    code = compile(source, "<nested>", "eval")
    built = CodeBuilder(code).reown(type("A", (), {}), type("B", (), {})).build()
    assert built.co_names == code.co_names


class TestNothingComposedNothingWritten:
    def test_composing_no_traits_writes_nothing(self):
        """F5: compose(cls) wrote an empty record onto cls."""

        class Person:
            pass

        before = dict(Person.__dict__)
        assert compose(Person) is Person
        assert dict(Person.__dict__) == before

    def test_a_failure_before_any_member_is_written_leaves_no_record(self):
        """F5: the record was created before the first write, so a failing first write left it behind."""

        class Meta(type):
            def __setattr__(cls, name, value):
                raise RuntimeError(f"the hook refuses {name}")

        class Person(metaclass=Meta):
            pass

        before = dict(Person.__dict__)
        with pytest.raises(RuntimeError, match="the hook refuses greet"):
            compose(Person, type("Trait", (), {"greet": _function("trait")}))
        assert dict(Person.__dict__) == before


class TestNoRollbackOnAPartialFailure:
    """Decided 2026-10-18 (docs/design/traits.md): when a write partway through a composition fails,
    the members already written are left in place; compose() does not undo them. Debith: rolling back
    "would lead to even more unexpected behavior". This pins that decision so a later change cannot
    quietly add rollback. It is characterization, not a bug: the behaviour is intended."""

    def test_members_written_before_a_failing_write_are_kept(self):
        class Meta(type):
            def __setattr__(cls, name, value):
                if name == "zzz":
                    raise RuntimeError("the hook refuses zzz")
                super().__setattr__(name, value)

        class Target(metaclass=Meta):
            pass

        # The members are written in the trait's dict order, so "aaa" is written before "zzz" fails.
        with pytest.raises(RuntimeError, match="the hook refuses zzz"):
            compose(Target, type("Trait", (), {"aaa": _function("a"), "zzz": _function("z")}))
        assert "aaa" in Target.__dict__          # kept: no rollback
        assert "zzz" not in Target.__dict__       # its own write failed
        assert set(Target.__dict__) & {"aaa", "zzz"} == {"aaa"}  # nothing else written either
        assert list(traits_of(Target)) == []      # the record is noted after every member: none here


class TestAnAttributeNamedLikeTheRecord:
    """F6: when the record was the class attribute `__pygim_traits__`, a class's own attribute of that
    name broke compose() with pybind11's cast error, and traits_of() returned it. Since 2026-10-19
    (R1, R3 in traits.md) the record is kept off the class: such an attribute is the class's own."""

    def test_a_classs_own_attribute_of_that_name_is_left_alone(self):
        class Person:
            __pygim_traits__ = "mine"

        compose(Person, type("Trait", (), {"greet": _function("trait")}))
        assert Person.__dict__["__pygim_traits__"] == "mine"
        assert list(traits_of(Person)) == ["greet"]

    def test_traits_of_does_not_report_an_attribute_that_is_not_a_record(self):

        class Person:
            __pygim_traits__ = "mine"

        record = traits_of(Person)
        assert type(record).__name__ == "TraitRecord" and list(record) == []
