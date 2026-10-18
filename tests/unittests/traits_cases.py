# -*- coding: utf-8 -*-
"""The cases pygim.traits and Python's naming rules are tested with, and the page that shows them.

Two kinds of case, kept apart:

- **generated** — each function's call space described as data and expanded into every call
  (``callspace.py``; the models in ``python_names_callspace.py`` and ``traits_callspace.py``), with
  expectations from Python itself. This is the coverage.
- **authored** — the tables below: hand-picked seed and edge rows, one per behaviour a reader asks
  about, with expectations written out by hand from the contracts (global #118). They stay as
  regression rows and as the page's worked examples; they are not the coverage.

The page docs/design/traits_cases.md is rendered from both — the generated cases' one-input
comparisons and counts, then the authored rows — by ``python tests/unittests/traits_cases.py``;
test_traits_cases_page.py keeps it current.
"""

from __future__ import annotations

import enum
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

import callspace
import python_names_callspace
import traits_callspace

M = __name__  # the module the traits below live in, as compose()'s messages name it
PAGE = Path(__file__).resolve().parents[2] / "docs" / "design" / "traits_cases.md"
RENDERED_ON = (3, 12)  # the expected values depend on the Python (co_qualname is 3.11's): the page is rendered on this one


# ── what a cell can hold, besides a plain value ─────────────────────────────────────────────────


class _Absent:
    """An argument not passed, or a result a refused call does not have."""

    def __repr__(self):
        return "—"


ABSENT = _Absent()


@dataclass(frozen=True)
class Raises:
    """An expected exception: its type and a fragment its message must contain ("" for the type alone)."""

    type: type
    message: str = ""

    def __repr__(self):
        return f"raises {self.type.__name__}" + (f": {self.message}" if self.message else "")


@dataclass(frozen=True)
class Ref:
    """A class, or a member of a class, defined in this module — named by its qualified name."""

    path: str

    def __repr__(self):
        return self.path

    def resolve(self):
        owner, _, member = self.path.partition(".")
        cls = globals()[owner]
        return cls.__dict__[member] if member else cls


@dataclass(frozen=True)
class Fresh:
    """A target class made anew for each call, so no case sees another's composition."""

    name: str

    def __repr__(self):
        return self.name

    def make(self):
        """The class, and an instance of it to call composed members on."""
        return TARGETS[self.name]()


class _Target:
    def __repr__(self):
        return "the target"


TARGET = _Target()  # compose() returns the target class it was given


class _ComposeCall:
    def __repr__(self):
        return "the compose() call"


COMPOSE_CALL = _ComposeCall()  # file:line of the call that composed the member


@dataclass(frozen=True)
class Table:
    """One function's cases: argument columns, then expected columns, one row per call."""

    title: str
    call: str
    columns: tuple
    arguments: int
    rows: tuple
    notes: tuple = ()

    def cases(self):
        """The rows for pytest.mark.parametrize("case", ...): a dict per row, keyed by column."""
        return [pytest.param(dict(zip(self.columns, row[:-1])), id=row[-1], marks=_skips(row))
                for row in self.rows]


def _skips(row):
    """A row whose target this Python cannot build is skipped, naming the Python it needs."""
    return [pytest.mark.skipif(sys.version_info < TARGET_NEEDS[cell.name],
                               reason=f"{cell.name} needs Python {'.'.join(map(str, TARGET_NEEDS[cell.name]))}")
            for cell in row if isinstance(cell, Fresh) and cell.name in TARGET_NEEDS]


def defined_at(ref):
    """``file:line`` of the code a trait member is made of — the function inside it, for a property
    or a classmethod — as the member's definition, not compose()'s answer, says."""
    member = ref.resolve()
    function = getattr(member, "fget", None) or getattr(member, "__func__", None) or member
    return f"{function.__code__.co_filename}:{function.__code__.co_firstlineno}"


# ── the classes the cases use ────────────────────────────────────────────────────────────────────
# Owners of code, for CodeBuilder: each compiles `self.__secret` with its own name (_Py_Mangle).


class Trait:
    def plain(self):
        return self.__secret  # co_names ('_Trait__secret',)

    def public(self):
        return self.value  # ('value',)

    def dunder(self):
        return self.__class__.__name__  # ('__class__', '__name__'): dunders are not mangled

    def nested(self):
        return (lambda: self.__secret)()  # outer (), the lambda ('_Trait__secret',)

    def others_private(self):
        return self._First__x  # ('_First__x',): First's private name, spelt by hand


class _Trait:
    def plain(self):
        return self.__secret  # ('_Trait__secret',): the leading underscore is dropped


class _:
    def plain(self):
        return self.__secret  # ('__secret',): a class named only with underscores mangles nothing


class Other:
    pass


class First:
    pass


class Second:
    pass


class Target:
    pass


class __Target:  # named with leading underscores; at module level the name itself is not mangled
    pass


# Traits, for compose(): classes written to be copied, never inherited from.


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


class Loud:
    def shout(self):
        return "LOUD"


class Ordered:
    def __lt__(self, other):
        return True


class Machinery:
    """A trait with nothing but its class machinery (__module__, __doc__, ...)."""


class WithConstant:
    factor = 2

    def scaled(self):
        return self.factor


class Polite:
    def hello(self):
        return "polite+" + super().hello()


class Doubled:
    def doubled(self):
        return self.x * 2


class Shouted:
    def shouted(self):
        return self.value.upper()


def not_a_trait(self):
    return None


# Targets, made fresh per call: (the class, a factory of instances).


def _person(*, with_shout=False):
    class Person:
        def __init__(self, name="ann"):
            self.__name = name  # stored as _Person__name

    if with_shout:
        Person.shout = lambda self: "mine"
    Person.__qualname__ = "Person"
    return Person, lambda: Person("ann")


def _child():
    class Base:
        def hello(self):
            return "base"

    class Child(Base):
        pass

    Child.__qualname__ = "Child"
    return Child, Child


def _point():
    @dataclass(frozen=True, slots=True)
    class Point:
        x: int

    Point.__qualname__ = "Point"
    return Point, lambda: Point(4)


def _colour():
    class Colour(enum.Enum):
        RED = "red"

    Colour.__qualname__ = "Colour"
    return Colour, lambda: Colour.RED


TARGETS = {
    "Person": _person,
    "Person with shout": lambda: _person(with_shout=True),
    "Child(Base)": _child,
    "Point": _point,
    "Colour": _colour,
}
TARGET_NEEDS = {"Point": (3, 10)}  # dataclass(slots=True) is Python 3.10's

GREETER_MEMBERS = ("greet", "kind", "name_length", "shout", "version")
R = Ref


# ── pygim.utils.is_dunder ──────────────────────────────────────────────────────────────────────────

IS_DUNDER = Table(
    title="pygim.utils.is_dunder",
    call="is_dunder(name)",
    columns=("name", "expected_result"),
    arguments=1,
    notes=("The expected results are Python's own rule, enum._is_dunder; the test also checks every "
           "row of this table against enum._is_dunder itself.",),
    rows=(
        # name          expected_result         id
        ("__ab__",      True,                   "baseline"),
        ("_ab__",       False,                  "leading_run_one"),        # vary the leading run
        ("___ab__",     False,                  "leading_run_three"),
        ("__ab_",       False,                  "trailing_run_one"),       # vary the trailing run
        ("__ab___",     False,                  "trailing_run_three"),
        ("____",        False,                  "body_empty"),             # vary the body
        ("__a__",       True,                   "body_one_character"),
        ("__a_b__",     True,                   "body_with_underscore"),
        ("__a__b__",    True,                   "body_with_double_underscore"),
        ("__lt__",      True,                   "protocol_method"),        # names Python uses
        ("__init__",    True,                   "initialiser"),
        ("__module__",  True,                   "class_machinery"),
        ("",            False,                  "empty"),                  # boundaries
        ("_",           False,                  "one_underscore"),
        ("__",          False,                  "two_underscores"),
        ("___",         False,                  "three_underscores"),
        ("_____",       False,                  "five_underscores"),
        ("__x",         False,                  "private_name"),           # other kinds of name
        ("__x_",        False,                  "private_name_trailing_one"),
        ("x__",         False,                  "trailing_only"),
        ("_x_",         False,                  "single_underscores"),
        ("_Trait__x",   False,                  "mangled_name"),
        ("x",           False,                  "plain"),
        (None,          Raises(TypeError),      "not_a_string"),           # invalid
    ),
)


# ── pygim.traits.CodeBuilder ─────────────────────────────────────────────────────────────────────

CODE_BUILDER = Table(
    title="pygim.traits.CodeBuilder",
    call="CodeBuilder(code).reown(steps[0].source, steps[0].target).reown(steps[1]...).build()",
    columns=("code", "steps[0].source", "steps[0].target", "steps[1].source", "steps[1].target",
             "result.co_names", "result.co_qualname", "result.inner.co_names", "result.inner.co_qualname",
             "original.co_names"),
    arguments=5,
    notes=("`code` is the named function's code object; `result.inner` is the code object nested in its "
           "constants (a lambda); `original.co_names` is the code passed in, after build() — a code object "
           "is never changed.",
           "co_qualname exists from Python 3.11; on 3.9 and 3.10 the qualified-name columns are not checked.",
           "Besides these literal expectations, every row but `source_not_the_owner` and "
           "`other_class_private_chained` is checked against Python's own compiler: the method's source "
           "compiled inside a class named like the last target must give the same names."),
    rows=(
        # code                     steps[0].source  steps[0].target  steps[1].source  steps[1].target
        #   result.co_names         result.co_qualname        result.inner.co_names  result.inner.co_qualname
        #   original.co_names       id
        (R("Trait.plain"),         R("Trait"),      R("Target"),     ABSENT,          ABSENT,
         ("_Target__secret",),     "Target.plain",           ABSENT,                ABSENT,
         ("_Trait__secret",),      "baseline"),
        (R("Trait.plain"),         R("Trait"),      R("__Target"),   ABSENT,          ABSENT,       # vary the target
         ("_Target__secret",),     "__Target.plain",         ABSENT,                ABSENT,
         ("_Trait__secret",),      "target_leading_underscores"),
        (R("Trait.plain"),         R("Trait"),      R("_"),          ABSENT,          ABSENT,
         ("__secret",),            "_.plain",                ABSENT,                ABSENT,
         ("_Trait__secret",),      "target_only_underscores"),
        (R("Trait.plain"),         R("Other"),      R("Target"),     ABSENT,          ABSENT,       # vary the source
         ("_Trait__secret",),      "Trait.plain",            ABSENT,                ABSENT,
         ("_Trait__secret",),      "source_not_the_owner"),
        (R("Trait.public"),        R("Trait"),      R("Target"),     ABSENT,          ABSENT,       # vary the code
         ("value",),               "Target.public",          ABSENT,                ABSENT,
         ("value",),               "code_public_name"),
        (R("Trait.dunder"),        R("Trait"),      R("Target"),     ABSENT,          ABSENT,
         ("__class__", "__name__"), "Target.dunder",         ABSENT,                ABSENT,
         ("__class__", "__name__"), "code_dunder_names"),
        (R("Trait.nested"),        R("Trait"),      R("Target"),     ABSENT,          ABSENT,
         (),                       "Target.nested",          ("_Target__secret",),  "Target.nested.<locals>.<lambda>",
         (),                       "code_nested"),
        (R("Trait.plain"),         ABSENT,          ABSENT,          ABSENT,          ABSENT,       # vary the steps
         ("_Trait__secret",),      "Trait.plain",            ABSENT,                ABSENT,
         ("_Trait__secret",),      "no_steps"),
        (R("Trait.plain"),         R("Trait"),      R("First"),      R("First"),      R("Second"),
         ("_Second__secret",),     "Second.plain",           ABSENT,                ABSENT,
         ("_Trait__secret",),      "two_steps"),
        (R("_Trait.plain"),        R("_Trait"),     R("Target"),     ABSENT,          ABSENT,       # the owner's spelling
         ("_Target__secret",),     "Target.plain",           ABSENT,                ABSENT,
         ("_Trait__secret",),      "owner_leading_underscore"),
        (R("_.plain"),             R("_"),          R("Target"),     ABSENT,          ABSENT,
         ("_Target__secret",),     "Target.plain",           ABSENT,                ABSENT,
         ("__secret",),            "owner_only_underscores"),
        (R("Trait.others_private"), R("Trait"),     R("Second"),     ABSENT,          ABSENT,       # interaction
         ("_First__x",),           "Second.others_private",  ABSENT,                ABSENT,
         ("_First__x",),           "other_class_private_direct"),
        (R("Trait.others_private"), R("Trait"),     R("First"),      R("First"),      R("Second"),
         ("_Second__x",),          "Second.others_private",  ABSENT,                ABSENT,
         ("_First__x",),           "other_class_private_chained"),
        ("return 1",               R("Trait"),      R("Target"),     ABSENT,          ABSENT,       # invalid
         Raises(TypeError, "CodeBuilder takes a code object, not str"), ABSENT, ABSENT, ABSENT,
         ABSENT,                   "code_not_code"),
        (R("Trait.plain"),         "Trait",         R("Target"),     ABSENT,          ABSENT,
         Raises(TypeError),        ABSENT,                   ABSENT,                ABSENT,
         ABSENT,                   "source_not_a_class"),
        (R("Trait.plain"),         R("Trait"),      "Target",        ABSENT,          ABSENT,
         Raises(TypeError),        ABSENT,                   ABSENT,                ABSENT,
         ABSENT,                   "target_not_a_class"),
    ),
)

# Rows Python's compiler cannot answer, and why.
COMPILER_ORACLE_DOES_NOT_APPLY = {
    "source_not_the_owner": "the builder is told another owner, so it moves nothing the compiler would",
    "other_class_private_chained": "the chain law's exception: after step one, First's name reads as First's own",
}


# ── pygim.traits.compose ──────────────────────────────────────────────────────────────────────────

_TRAIT_KINDS = "a trait composes functions, properties, classmethods and staticmethods"

COMPOSE = Table(
    title="pygim.traits.compose",
    call="compose(target, traits[0], traits[1], override=override)",
    columns=("target", "traits[0]", "traits[1]", "override", "expected_result", "effect: written", "effect: traits_of"),
    arguments=4,
    notes=("`effect: written` is the names the call set in the target's own namespace, its record "
           "`__pygim_traits__` aside; a refused call writes nothing. `effect: traits_of` is "
           "list(traits_of(target)) after the call.",),
    rows=(
        # target                       traits[0]           traits[1]   override
        #   expected_result                                                     effect: written   effect: traits_of   id
        (Fresh("Person"),              R("Greeter"),       ABSENT,     False,
         TARGET,                                                                GREETER_MEMBERS,  GREETER_MEMBERS,    "baseline"),
        (Fresh("Person"),              R("Loud"),          ABSENT,     False,                      # vary traits[0]
         TARGET,                                                                ("shout",),       ("shout",),         "trait_one_function"),
        (Fresh("Person"),              R("Ordered"),       ABSENT,     False,
         TARGET,                                                                ("__lt__",),      ("__lt__",),        "trait_name_the_target_inherits"),
        (Fresh("Person"),              R("Machinery"),     ABSENT,     False,
         TARGET,                                                                (),               (),                 "trait_only_machinery"),
        (Fresh("Person"),              R("WithConstant"),  ABSENT,     False,
         Raises(TypeError, f"WithConstant.factor is int; {_TRAIT_KINDS}"),      (),               (),                 "trait_with_a_constant"),
        (Fresh("Person"),              R("not_a_trait"),   ABSENT,     False,
         Raises(TypeError, "compose takes classes as traits, not function"),    (),               (),                 "trait_not_a_class"),
        (Fresh("Person"),              ABSENT,             ABSENT,     False,
         TARGET,                                                                (),               (),                 "no_traits"),
        (Fresh("Person with shout"),   R("Greeter"),       ABSENT,     False,                      # vary the target
         Raises(ValueError, f"Person already has shout; {M}.Greeter would replace it (pass override=True)"),
                                                                                (),               (),                 "target_has_the_name"),
        (Fresh("Person with shout"),   R("Greeter"),       ABSENT,     True,                       # vary override
         TARGET,                                                                GREETER_MEMBERS,  GREETER_MEMBERS,    "target_has_the_name_override"),
        (Fresh("Person"),              R("Greeter"),       R("Loud"),  False,                      # vary traits[1]
         Raises(ValueError, f"{M}.Greeter and {M}.Loud both define shout (pass override=True to let the later win)"),
                                                                                (),               (),                 "two_traits_one_name"),
        (Fresh("Person"),              R("Greeter"),       R("Loud"),  True,                       # vary override
         TARGET,                                                                GREETER_MEMBERS,  GREETER_MEMBERS,    "two_traits_one_name_override"),
        (Fresh("Person with shout"),   R("Greeter"),       R("Loud"),  True,                       # interaction
         TARGET,                                                                GREETER_MEMBERS,  GREETER_MEMBERS,    "target_and_two_traits_override"),
        (Fresh("Child(Base)"),         R("Polite"),        ABSENT,     False,                      # kinds of target
         TARGET,                                                                ("hello",),       ("hello",),         "target_with_a_base"),
        (Fresh("Point"),               R("Doubled"),       ABSENT,     False,
         TARGET,                                                                ("doubled",),     ("doubled",),       "target_frozen_slots_dataclass"),
        (Fresh("Colour"),              R("Shouted"),       ABSENT,     False,
         TARGET,                                                                ("shouted",),     ("shouted",),       "target_enum"),
        ("Person",                     R("Greeter"),       ABSENT,     False,                      # invalid
         Raises(TypeError),                                                     ABSENT,           ABSENT,             "target_not_a_class"),
    ),
)

COMPOSED_MEMBERS = Table(
    title="pygim.traits.compose: the composed members, called",
    call="compose(target, traits[0], traits[1], override=override), then instance.<member>",
    columns=("target", "traits[0]", "traits[1]", "override", "member", "expected_result"),
    arguments=5,
    notes=("`member` is what is read on an instance of the target: `name()` calls it, `name(other)` calls "
           "it with a second instance, a dotted name reads attributes.",),
    rows=(
        # target                      traits[0]       traits[1]   override  member                 expected_result   id
        (Fresh("Person"),             R("Greeter"),   ABSENT,     False,    "greet()",             "hello ann",      "baseline"),
        (Fresh("Person"),             R("Greeter"),   ABSENT,     False,    "shout()",             "ANN",            "member_with_nested_code"),  # vary member
        (Fresh("Person"),             R("Greeter"),   ABSENT,     False,    "name_length",         3,                "member_property"),
        (Fresh("Person"),             R("Greeter"),   ABSENT,     False,    "kind()",              "Person",         "member_classmethod"),
        (Fresh("Person"),             R("Greeter"),   ABSENT,     False,    "version()",           1,                "member_staticmethod"),
        (Fresh("Person"),             R("Greeter"),   ABSENT,     False,    "greet.__qualname__",  "Person.greet",   "member_qualified_name"),
        (Fresh("Person with shout"),  R("Greeter"),   ABSENT,     True,     "shout()",             "ANN",            "override_replaces_the_targets"),
        (Fresh("Person"),             R("Greeter"),   R("Loud"),  True,     "shout()",             "LOUD",           "override_the_later_trait_wins"),
        (Fresh("Person"),             R("Ordered"),   ABSENT,     False,    "__lt__(other)",       True,             "protocol_method"),
        (Fresh("Child(Base)"),        R("Polite"),    ABSENT,     False,    "hello()",             "polite+base",    "super_resolves_in_the_target"),
        (Fresh("Point"),              R("Doubled"),   ABSENT,     False,    "doubled()",           8,                "frozen_slots_dataclass"),
        (Fresh("Colour"),             R("Shouted"),   ABSENT,     False,    "shouted()",           "RED",            "enum_member"),
    ),
)


# ── pygim.traits.traits_of ────────────────────────────────────────────────────────────────────────

TRAITS_OF = Table(
    title="pygim.traits.traits_of",
    call="compose(target, traits[0], traits[1], override=override); traits_of(target)[name]",
    columns=("target", "traits[0]", "traits[1]", "override", "name", "expected_result.trait",
             "expected_result.defined", "expected_result.composed"),
    arguments=5,
    notes=("`expected_result.defined` names the trait member; its expected value is `file:line` of the "
           "code that member is made of. `expected_result.composed` is `file:line` of the compose() call.",),
    rows=(
        # target           traits[0]       traits[1]   override  name           .trait                .defined                   .composed      id
        (Fresh("Person"),  R("Greeter"),   ABSENT,     False,    "greet",       f"{M}.Greeter",       R("Greeter.greet"),        COMPOSE_CALL,  "baseline"),
        (Fresh("Person"),  R("Greeter"),   ABSENT,     False,    "name_length", f"{M}.Greeter",       R("Greeter.name_length"),  COMPOSE_CALL,  "name_property"),     # vary name
        (Fresh("Person"),  R("Greeter"),   ABSENT,     False,    "kind",        f"{M}.Greeter",       R("Greeter.kind"),         COMPOSE_CALL,  "name_classmethod"),
        (Fresh("Person"),  R("Greeter"),   ABSENT,     False,    "missing",     Raises(KeyError),     ABSENT,                    ABSENT,        "name_not_composed"),
        (Fresh("Person"),  R("Greeter"),   R("Loud"),  True,     "shout",       f"{M}.Loud",          R("Loud.shout"),           COMPOSE_CALL,  "later_trait_recorded"),  # vary traits
        (Fresh("Person"),  ABSENT,         ABSENT,     False,    "greet",       Raises(KeyError),     ABSENT,                    ABSENT,        "nothing_composed"),
    ),
)

TABLES = (IS_DUNDER, CODE_BUILDER, COMPOSE, COMPOSED_MEMBERS, TRAITS_OF)

LIMITS = (
    "The domains are finite and chosen: every class name, name and qualified name is one of the atoms or "
    "generated shapes listed above; no claim is made for strings outside them, nor for classes, code objects or "
    "traits beyond what the factories make.",
    "Expectations for the inputs Python's compiler cannot take (a name or class name that is not an identifier, "
    "a qualified name its class does not own) come from the written contract, and the cases say so.",
    "Two models are reductions, said in their notes: CodeBuilder's two-step chains (one template, the names a step "
    "can move) and compose()'s two traits (three member kinds); the one-step and one-trait models cover the rest.",
    "A trait member written in C++ (the quantity traits to come), composing into a pybind11 class or a class with a "
    "metaclass other than Enum's, and composing from several threads have no model.",
    "On Python 3.9 the frozen slots dataclass target is excluded (dataclass(slots=True) is 3.10's); before 3.11 a "
    "code object has no co_qualname, so qualified names are not compared there.",
    "One-input comparisons explain one input's effect from a baseline; the counts above them are the coverage, "
    "interactions included.",
    "The native product of utils/python_names.h is called and branch-measured only by "
    "tests/static/run_python_names_callspace.py, which needs GCC 16 (-freflection); every build proves the same "
    "product at compile time. Branch coverage of the pybind11 adapters (traits/compose.h, traits/adapter.h) is not "
    "measured. utils/core_utils.h keeps authored tables only.",
)


# ── the page: the generated cases first, then the authored rows ─────────────────────────────────


def _cell(value):
    if value is ABSENT:
        return "—"
    text = repr(value).replace("`", "&#96;").replace("|", "&#124;")
    return f"`{text}`"


def _markdown_table(headers, rows):
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |"]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    return lines + [""]


def _code(text):
    return "`" + str(text).replace("`", "&#96;").replace("|", "&#124;") + "`"


# Where each model's one-input comparisons start: a call every reader can predict.
BASELINES = {
    "pygim::python_names::is_dunder": {"name": "__x__"},
    "pygim::python_names::owner_of": {"class_name": "_Trait"},
    "pygim::python_names::is_private_name": {"name": "__x"},
    "pygim::python_names::mangled": {"class_name": "Trait", "name": "__x"},
    "pygim::python_names::unmangled": {"class_name": "Trait", "name": "_Trait__x"},
    "pygim::python_names::respelt": {"from": "Trait", "to": "Target", "name": "_Trait__x"},
    "pygim::python_names::reowned": {"from": "Trait", "to": "Target", "name": "_Trait__x"},
    "pygim::python_names::requalified": {"from": "Trait", "to": "Target", "qualname": "Trait.f"},
    "pygim.traits.CodeBuilder: one step": {"template": "attribute", "name": "__x", "owner": "Trait",
                                           "step": ("Trait", "Target")},
    "pygim.traits.CodeBuilder: two steps": {"name": "__x", "owner": "Trait", "steps[0]": ("Trait", "Target"),
                                            "steps[1]": ("Target", "Outer.Target")},
    "pygim.traits.CodeBuilder: refusals": {"position": "code", "wrong": "text"},
    "pygim.traits.compose: one trait": {"target": "plain", "traits[0]": ("function", "private", "greet"),
                                        "override": False},
    "pygim.traits.compose: two traits": {"target": "plain", "traits[0]": ("function", "private", "greet"),
                                         "traits[1]": ("function", "private", "hello"), "override": False},
    "pygim.utils.is_dunder": {"name": "__x__"},
}


def _shown(model, value):
    """An expected outcome as a page cell."""
    if not isinstance(value, dict):
        return _cell(value)
    if "raises" in value:
        message = value.get("message")
        return _code(f"raises {value['raises']}" + (f": {message}" if message else ""))
    if "tree" in value:
        tree = value["tree"]
        qualnames = tree["co_qualname"] or [None] * len(tree["co_names"])
        return " · ".join(_code(f"{tuple(names)} {qualname or ''}".strip())
                          for names, qualname in zip(tree["co_names"], qualnames))
    if "written" in value:
        members = "; ".join(f"{name} → {outcome[1]!r} ({qualname})" if outcome[0] == "value"
                            else f"{name} → raises {outcome[1]} ({qualname})"
                            for name, (outcome, qualname) in value.get("members", {}).items())
        record = ", ".join(f"{name} ← {trait.rsplit('.', 1)[-1]}" for name, trait in value["record"].items())
        return _code(f"writes {', '.join(value['written']) or 'nothing'}" + (f"; record {record}" if record else "")
                     + (f"; {members}" if members else ""))
    if "returns" in value:
        return _cell(value["returns"])
    return _code(value)


def _model_section(model, records, called_by):
    lines = [f"### {model.name}", "", f"`{model.call}` — called by {called_by}.", ""]
    for note in model.notes:
        lines += [note, ""]
    lines += _markdown_table(["parameter", "domain", "how it is made", "values"], [
        [_code(p.name), p.domain.name, _made(p.domain), str(len(p.domain.expand()))] for p in model.parameters])
    calls, excluded = callspace.required(model)
    product = 1
    for p in model.parameters:
        product *= len(callspace.rebuild(p.domain))
    oracles = {}
    for record in records:
        oracles[record["oracle"]] = oracles.get(record["oracle"], 0) + 1
    reasons = "; ".join(f"{reason} ({count})" for reason, count in excluded.items())
    lines += [f"Product {product}, excluded {sum(excluded.values())}" + (f" — {reasons}" if reasons else "")
              + f"; required {len(calls)}. Expected by: "
              + ", ".join(f"{oracle} {count}" for oracle, count in sorted(oracles.items())) + ".", ""]
    baseline = BASELINES[model.name]
    assert any(callspace.key(r["arguments"]) == callspace.key(baseline) for r in records), model.name
    lines += ["Baseline: " + "; ".join(f"`{k}` = {_cell(v)}" for k, v in baseline.items()) + ".", ""]
    for group in callspace.one_input_comparisons(model, records, baseline):
        varied = group["varied"]
        lines += [f"#### Vary `{varied}`", ""]
        lines += _markdown_table([varied, "expected", "by"], [
            [_cell(r["arguments"][varied]), _shown(model, r["expected"])
             + (" ⚠ divergence" if r.get("divergence") else ""), r["oracle"]] for r in group["rows"]])
    return lines


def _made(domain):
    if domain.provenance == "manual":
        return f"manual: {domain.why}"
    parts = getattr(domain, "parts", None)
    if parts:
        return "generated from " + " and ".join(_made_part(p) for p in parts)
    return "generated: " + _made_part(domain)


def _made_part(domain):
    if getattr(domain, "factors", None):
        factors = " × ".join(f"{name} ({len(d.expand())}, {d.provenance})" for name, d in domain.factors)
        return f"{domain.name} = {factors}" + (f", less {domain.dropped}" if getattr(domain, "keep", None) else "")
    if getattr(domain, "element", None) is not None:
        return f"{domain.name}: sequences of {domain.element.name} up to {domain.max_length}"
    return f"{domain.name} ({len(domain.expand())}, {domain.provenance})"


def one_input_groups(table):
    """Groups of authored rows whose arguments differ in exactly one column, the others fixed and equal."""
    groups = {}
    for row in table.rows:
        arguments = row[: table.arguments]
        for varied in range(table.arguments):
            fixed = tuple((table.columns[i], repr(arguments[i])) for i in range(table.arguments) if i != varied)
            group = groups.setdefault((varied, fixed), {"varied": varied, "fixed": fixed, "rows": []})
            group["rows"].append(row)
    return [g for g in groups.values() if len({repr(r[g["varied"]]) for r in g["rows"]}) > 1]


def render_table(table):
    """An authored table: every row, as written."""
    lines = [f"### {table.title}", "", f"`{table.call}`", ""]
    for note in table.notes:
        lines += [note, ""]
    lines += _markdown_table([*table.columns, "case"], [[*(_cell(v) for v in row[:-1]), row[-1]] for row in table.rows])
    return lines


def _cell_text(text):
    return "—" if text == "—" else "`" + text.replace("`", "&#96;").replace("|", "&#124;") + "`"


def render_page():
    python_names = python_names_callspace.cases()
    traits = traits_callspace.runs()
    lines = [
        "<!-- Generated from tests/unittests/traits_cases.py by `python tests/unittests/traits_cases.py`, on Python "
        f"{RENDERED_ON[0]}.{RENDERED_ON[1]};",
        "     tests/unittests/test_traits_cases_page.py fails when it is not current. Edit the models and cases, not "
        "this page. -->",
        "",
        "# pygim.traits and Python's naming rules: the test cases",
        "",
        "Two kinds of case. **Generated** cases are the coverage: each function's call space is described as data — "
        "domains of argument values, some chosen by hand (*manual*), most expanded from declared factors and bounds "
        "(*generated*) — and expanded into every call; a checker rebuilds the space from the declaration and fails a "
        "run that misses, duplicates or adds a call, or whose domain shrank. **Authored** rows, further down, are "
        "hand-picked seed and edge cases kept as regression rows and worked examples.",
        "",
        "Every expected result comes from Python, never from pygim: `enum._is_dunder`; Python's compiler — a name "
        "compiled inside `class <C>:`, a template's qualified name, a trait's members written into the target's own "
        "source — or, where the compiler cannot be asked, the written contract. The `by` column says which.",
        "",
        "## The models",
        "",
    ]
    rows = []
    for function, model in python_names_callspace.MODELS.items():
        records = python_names[function]
        rows.append([_code(model.name), str(len(records)), "every build (static_assert); GCC 16 probe"])
    for name, (model, _) in traits_callspace.MODELS.items():
        rows.append([_code(model.name), str(len(traits[name].records)), "pytest"])
    lines += _markdown_table(["model", "calls", "called by"], rows)
    lines += [f"{sum(len(r) for r in python_names.values())} calls of utils/python_names.h and "
              f"{sum(len(r.records) for r in traits.values())} of the Python API.", ""]
    lines += ["## utils/python_names.h", "",
              "Every function the namespace declares, found by GCC 16 reflection "
              "(tests/static/run_python_names_callspace.py); a function or parameter without a model fails to "
              "compile there. Each parameter's domain is chosen by its role — its name in the declaration — as every "
              "parameter is a `std::string_view`.", ""]
    for function, model in python_names_callspace.MODELS.items():
        lines += _model_section(model, python_names[function], "every build and the GCC 16 probe")
    lines += ["## The Python API", "",
              "Classes and code objects are made by factories from the data in each cell: a code template compiled "
              "inside the owner class, a class from its qualified name, a target from its source. Targets and traits "
              "are made fresh for every case. Besides the cells shown, every CodeBuilder case expects the rest of "
              "each code object unchanged, the code passed in unchanged and two builds equal; every compose() case "
              "that writes expects the traits untouched and the record's `defined` and `composed` locations.", ""]
    for name, (model, _) in traits_callspace.MODELS.items():
        lines += _model_section(model, traits[name].records, "pytest")
    lines += ["## Found by the generated cases", "",
              "Calls whose outcome differs from the expectation, kept visible and not yet decided: each case named "
              "must keep failing and no other may (the checker reports either).", ""]
    for divergence in traits_callspace.DIVERGENCES:
        count = sum(1 for r in traits[divergence.model].records if r.get("divergence") == divergence.finding)
        lines += [f"- `{divergence.model}`, {count} case{'' if count == 1 else 's'}: {divergence.finding}"]
    lines += ["", "## Authored seed and edge rows", "",
              "Written by hand, one row per behaviour a reader asks about; tested by test_traits.py, "
              "test_traits_compose.py and test_utils.py.", ""]
    for table in TABLES:
        lines += render_table(table)
    lines += ["## What these cases do not cover", ""]
    lines += [f"- {limit}" for limit in LIMITS] + [""]
    return "\n".join(lines)


def write_page():
    PAGE.write_text(render_page(), encoding="utf-8")
    return PAGE


if __name__ == "__main__":
    # Imported under its own name, so the trait names on the page are the ones the tests see.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    print(f"written: {importlib.import_module('traits_cases').write_page()}")
