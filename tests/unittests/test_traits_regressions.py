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
    def test_a_trait_cannot_define_the_record(self):
        """F2: a member named __pygim_traits__ replaced the record mid-write — a use after free."""
        outcome = bounded_child.run("""
            from pygim.traits import compose, traits_of
            class Target: pass
            Trait = type("Trait", (), {"a": lambda self: 1, "__pygim_traits__": lambda self: 2})
            try:
                compose(Target, Trait)
            except ValueError as error:
                print("refused:", error)
            print("written:", sorted(k for k in vars(Target) if k in ("a", "__pygim_traits__")))
            print("record:", list(traits_of(Target)))
        """)
        assert not outcome.crashed and not outcome.timed_out, outcome.stderr
        assert outcome.stdout.splitlines() == [
            "refused: Trait.__pygim_traits__ is compose()'s record of what it composed; a trait cannot define it",
            "written: []", "record: []"]

    def test_a_hook_that_deletes_the_record_mid_write_cannot_crash(self):
        """F2: compose() kept a reference into the record across writes a hook could undo."""
        outcome = bounded_child.run("""
            from pygim.traits import compose, traits_of
            class Meta(type):
                def __setattr__(cls, name, value):
                    super().__setattr__(name, value)
                    if name == "b" and "__pygim_traits__" in cls.__dict__:
                        delattr(cls, "__pygim_traits__")  # the hook's own doing, not compose()'s
            class Target(metaclass=Meta): pass
            compose(Target, type("Trait", (), {"a": lambda self: 1, "b": lambda self: 2, "c": lambda self: 3}))
            print("written:", sorted(k for k in vars(Target) if k in "abc"))
        """)
        assert not outcome.crashed and not outcome.timed_out, outcome.stderr
        assert outcome.stdout.splitlines() == ["written: ['a', 'b', 'c']"]


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


class TestAnAttributeNamedLikeTheRecord:
    def test_compose_refuses_a_class_whose_own_attribute_is_not_a_record(self):
        """F6: compose() failed with pybind11's cast error, naming nothing."""

        class Person:
            __pygim_traits__ = "mine"

        with pytest.raises(TypeError, match=r"Person.__pygim_traits__ is str; compose\(\) keeps its record there"):
            compose(Person, type("Trait", (), {"greet": _function("trait")}))
        assert "greet" not in Person.__dict__ and Person.__dict__["__pygim_traits__"] == "mine"

    def test_traits_of_does_not_report_an_attribute_that_is_not_a_record(self):
        """F6: traits_of() returned the attribute itself, whatever it was."""

        class Person:
            __pygim_traits__ = "mine"

        record = traits_of(Person)
        assert type(record).__name__ == "TraitRecord" and list(record) == []
