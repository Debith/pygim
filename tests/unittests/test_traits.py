# -*- coding: utf-8 -*-
"""``pygim.traits.CodeBuilder``: a code object rebuilt so it belongs to another class.

Python compiles a private name (``self.__secret``) inside a class as ``_Class__secret``. A method's
code copied into another class keeps the first class's spelling, so it reads an attribute the new
class never stored. The builder rewrites those names — and the qualified name — for the new owner,
in the code and in every code object nested in it.

The cases are ``traits_cases.CODE_BUILDER``: one row per call, argument columns then expected
columns (global #118). Each row is checked against its literal expectations, and — where it applies
— against Python's own compiler, compiling the same method inside a class named like the target.
"""

import inspect
import re
import sys
import textwrap
import types

import pytest

from pygim.traits import CodeBuilder
from traits_cases import ABSENT, CODE_BUILDER, COMPILER_ORACLE_DOES_NOT_APPLY, Raises, Ref

HAS_QUALNAME = sys.version_info >= (3, 11)  # a code object has co_qualname from Python 3.11


def _value(cell):
    """A cell as the call receives it: a named function's code, a named class, or the raw value."""
    if not isinstance(cell, Ref):
        return cell
    resolved = cell.resolve()
    return resolved.__code__ if isinstance(resolved, types.FunctionType) else resolved


def _build(case):
    builder = CodeBuilder(_value(case["code"]))
    for i in (0, 1):
        if case[f"steps[{i}].source"] is not ABSENT:
            builder.reown(_value(case[f"steps[{i}].source"]), _value(case[f"steps[{i}].target"]))
    return builder.build()


def _inner(code):
    """The code object nested in `code`'s constants, or None."""
    nested = [constant for constant in code.co_consts if isinstance(constant, types.CodeType)]
    return nested[0] if nested else None


def _compiled_in(class_name, function):
    """`function`'s source compiled inside a class named `class_name`: Python's own spelling."""
    source = textwrap.dedent(inspect.getsource(function))
    namespace = {}
    exec(compile(f"class {class_name}:\n" + textwrap.indent(source, "    "), "<compiled>", "exec"), namespace)
    return namespace[class_name].__dict__[function.__name__].__code__


# --- the table, checked against its literal expectations --------------------------------------


@pytest.mark.parametrize("case", CODE_BUILDER.cases())
def test_code_builder(case):
    expected = case["result.co_names"]
    if isinstance(expected, Raises):
        with pytest.raises(expected.type, match=re.escape(expected.message) if expected.message else None):
            _build(case)
        return
    built = _build(case)
    inner = _inner(built)
    assert built.co_names == expected
    assert (inner.co_names if inner else ABSENT) == case["result.inner.co_names"]
    if HAS_QUALNAME:
        assert built.co_qualname == case["result.co_qualname"]
        assert (inner.co_qualname if inner else ABSENT) == case["result.inner.co_qualname"]
    assert _value(case["code"]).co_names == case["original.co_names"]  # the code passed in is unchanged


# --- the same rows, checked against Python's compiler -------------------------------------------

ORACLE_CASES = [
    case for case in CODE_BUILDER.cases()
    if case.id not in COMPILER_ORACLE_DOES_NOT_APPLY and not isinstance(case.values[0]["result.co_names"], Raises)
]


@pytest.mark.parametrize("case", ORACLE_CASES)
def test_code_builder_spells_names_as_pythons_compiler_does(case):
    function = case["code"].resolve()
    last_target = next((case[f"steps[{i}].target"] for i in (1, 0) if case[f"steps[{i}].target"] is not ABSENT), None)
    owner = last_target.resolve().__name__ if last_target else function.__qualname__.split(".")[0]
    compiled, built = _compiled_in(owner, function), _build(case)
    assert built.co_names == compiled.co_names
    assert (_inner(built) and _inner(built).co_names) == (_inner(compiled) and _inner(compiled).co_names)
    if HAS_QUALNAME:
        assert built.co_qualname == compiled.co_qualname


def test_the_rows_the_compiler_cannot_answer_exist():
    """The exemptions above name real rows; a renamed row would silently widen them."""
    assert set(COMPILER_ORACLE_DOES_NOT_APPLY) <= {case.id for case in CODE_BUILDER.cases()}


# --- what no single row shows -----------------------------------------------------------------


def test_building_twice_gives_equal_code():
    builder = CodeBuilder(_value(Ref("Trait.nested"))).reown(_value(Ref("Trait")), _value(Ref("Target")))
    assert builder.build() == builder.build()


def test_a_builder_with_no_steps_builds_an_equal_copy():
    code = _value(Ref("Trait.nested"))
    assert CodeBuilder(code).build() == code
