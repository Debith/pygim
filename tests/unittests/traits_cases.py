# -*- coding: utf-8 -*-
"""The cases pygim.traits and pygim.utils.is_dunder are tested with, as argument tables.

A table's rows are calls: its argument columns first, then what the call is expected to give and
any separately observable effect, then the case's id (global #118). A baseline row comes first in
each table, and groups of rows below it change one input while the others stay as they were;
interaction rows change several. The expected values come from the contracts — Python's own rules
(the compiler's private-name mangling, enum._is_dunder) and the refusals docs/design/traits.md
documents — written out by hand, never copied from what the code returns.

The same rows feed the tests (test_traits.py, test_traits_compose.py, test_utils.py) and the page
docs/design/traits_cases.md, which ``python tests/unittests/traits_cases.py`` regenerates and
test_traits_cases_page.py keeps current.
"""

from __future__ import annotations

import enum
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

M = __name__  # the module the traits below live in, as compose()'s messages name it
PAGE = Path(__file__).resolve().parents[2] / "docs" / "design" / "traits_cases.md"


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
    "Members a trait may hold are functions, properties, classmethods and staticmethods; a trait "
    "member written in C++ (the quantity traits to come) has no case yet.",
    "Zero-argument super() is covered for a function member; not inside a property or a classmethod.",
    "A keyword argument named with two leading underscores in a call is not mangled by Python, so "
    "CodeBuilder has nothing to do for it; no row shows that.",
    "Composing into a class with a custom metaclass other than Enum's, into a pybind11 class, and from "
    "several threads at once is not covered.",
    "On Python 3.9 the rows whose target is Point, a frozen slots dataclass, are skipped: "
    "dataclass(slots=True) is Python 3.10's.",
    "One-input groups explain one input's effect; they do not replace the interaction rows, and no "
    "table is the full product of its columns.",
    "The C++ core (utils/python_names.h, utils/core_utils.h) is proven by its own case tables in "
    "tests/static; their rows are not on this page. Branch coverage of the C++ adapters is not measured.",
)


# ── the page, rendered from the tables ───────────────────────────────────────────────────────────


def _cell(value):
    if value is ABSENT:
        return "—"
    text = repr(value).replace("`", "&#96;").replace("|", "&#124;")
    return f"`{text}`"


def _markdown_table(headers, rows):
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |"]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    return lines + [""]


def one_input_groups(table):
    """Groups of rows whose arguments differ in exactly one column, the others fixed and equal."""
    groups = {}
    for row in table.rows:
        arguments = row[: table.arguments]
        for varied in range(table.arguments):
            fixed = tuple((table.columns[i], repr(arguments[i])) for i in range(table.arguments) if i != varied)
            group = groups.setdefault((varied, fixed), {"varied": varied, "fixed": fixed, "rows": []})
            group["rows"].append(row)
    return [g for g in groups.values() if len({repr(r[g["varied"]]) for r in g["rows"]}) > 1]


def render_table(table):
    expected = table.columns[table.arguments:]
    lines = [f"## {table.title}", "", f"`{table.call}`", ""]
    for note in table.notes:
        lines += [note, ""]
    groups = one_input_groups(table)
    if groups:
        lines += ["### Change one input", ""]
        for group in groups:
            varied = table.columns[group["varied"]]
            fixed = "; ".join(f"`{name}` = {_cell_text(text)}" for name, text in group["fixed"])
            lines += [f"#### Vary `{varied}`", "", f"Fixed: {fixed}.", ""]
            lines += _markdown_table([varied, *expected, "case"], [
                [_cell(row[group["varied"]]), *(_cell(v) for v in row[table.arguments:-1]), row[-1]]
                for row in group["rows"]])
    lines += ["### Every case", ""]
    lines += _markdown_table([*table.columns, "case"], [[*(_cell(v) for v in row[:-1]), row[-1]] for row in table.rows])
    return lines


def _cell_text(text):
    return "—" if text == "—" else "`" + text.replace("`", "&#96;").replace("|", "&#124;") + "`"


def render_page():
    lines = [
        "<!-- Generated from tests/unittests/traits_cases.py by `python tests/unittests/traits_cases.py`;",
        "     tests/unittests/test_traits_cases_page.py fails when it is not current. Edit the cases, not this page. -->",
        "",
        "# pygim.traits and Python's naming rules: the test cases",
        "",
        "Each row is one call: its arguments, then what it is expected to give, then the case's id. "
        "`—` is an argument not passed or a result a refused call does not have; `raises T: text` is the "
        "exception expected, with a fragment of its message. Expected values come from the contracts — "
        "Python's own rules and the refusals [traits.md](traits.md) documents — never from what the code "
        "returned (global #118).",
        "",
        "\"Change one input\" shows the rows that differ from each other in one argument only, the others "
        "fixed as stated above each table; \"Every case\" shows them all, interactions included.",
        "",
    ]
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
