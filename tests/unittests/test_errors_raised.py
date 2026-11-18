# -*- coding: utf-8 -*-
"""Every failure pygim raises is a class of pygim.errors (docs/design/errors.md): one row per raise
site that Python can reach, the call first, then the class it raises and what it says. Each class is
also the builtin its meaning needs, so `except TypeError` and the like keep working; the rows check
pygim's class, which is the stronger claim."""

import math
import traceback

import pytest

import pygim
from pygim import path, utils
from pygim.core.funcs import get_schema
from pygim.each import each
from pygim.errors import (
    ArgumentError,
    MissingAttributeError,
    GimOptionError,
    GimOverflowError,
    InvalidStateError,
    InvalidValueError,
    ParseError,
    SignatureError,
    UnrecognizedTypeError,
)
from pygim.ioc import Container


def _merge_sum_into_text():
    d = utils.gimdict({"k": 1})
    d.set_strategy("k", "sum")
    d.merge_in("k", "x")


def _lap_after_finish():
    timer = utils.QuickTimer("t", report=utils.Report.silent())
    timer.finish()
    timer.start_lap("x")


CASES = [
    # (call, raises, says)
    (lambda: utils.gimdict(5), UnrecognizedTypeError, "gimdict's initial: Expected a mapping, got `5 [int]`"),
    (lambda: utils.gimdict().set_default_strategy(5), GimOptionError, "Given input ``5`` not among: sum, max, min, replace"),
    (_merge_sum_into_text, ArgumentError, "sum strategy failed for incompatible values"),
    (lambda: utils.Duration(ms=True), UnrecognizedTypeError, "ms: Expected a real number, got `True [bool]`"),
    (lambda: utils.Duration(ms="x"), UnrecognizedTypeError, "ms: Expected a real number, got `'x' [str]`"),
    (lambda: utils.Duration(ns=2**63), GimOverflowError, "a duration past 64 bits of nanoseconds (about 292 years)"),
    (lambda: utils.Duration(s=1e300), GimOverflowError, "a duration past 64 bits of nanoseconds (about 292 years)"),
    (lambda: utils.Duration(s=math.nan), InvalidValueError, "a duration of a NaN or an infinity"),
    (lambda: utils.Duration(parsecs=1), ArgumentError, "unknown unit 'parsecs'; the units are yr, wk, day, hr, min, s, ms, us and ns"),
    (lambda: utils.ManualTime().advance(utils.Duration(ms=1), ms=2), ArgumentError, "advance() takes a Duration or units, not both"),
    (lambda: utils.Report.stderr().on_started(print), ArgumentError, "only a Report.hooks() report takes hooks"),
    (lambda: utils.QuickTimer("t", clock=5), UnrecognizedTypeError, "clock: Expected a ManualTime, or None for the monotonic clock, got `5 [int]`"),
    (lambda: utils.QuickTimer("t", report=5), UnrecognizedTypeError, "report: Expected a Report, or None for Report.stderr(), got `5 [int]`"),
    (lambda: utils.Report.both(5), UnrecognizedTypeError, "Report.both: Expected a Report, got `5 [int]`"),
    (_lap_after_finish, InvalidStateError, "the timer has finished; a lap cannot start after it"),
    (lambda: utils.QuickTimer("t", report=utils.Report.silent()).stop_lap(), InvalidStateError, "no lap is running"),
    (lambda: utils.calculate_rate(1, "kb", 0, "s"), InvalidValueError, "duration must be positive"),
    (lambda: each([1, 2]).nope, MissingAttributeError, None),
    (lambda: getattr(each([1, 2]), "__nope__"), MissingAttributeError, "Cannot access dunder attributes with `each`"),
    (lambda: pygim.no_such_thing, MissingAttributeError, "module 'pygim' has no attribute 'no_such_thing'"),
    (lambda: get_schema(lambda x: x), SignatureError, None),
]


@pytest.mark.parametrize("call, raises, says", CASES, ids=[f"{i:02}-{c[1].__name__}" for i, c in enumerate(CASES)])
def test_each_failure_raises_its_pygim_class(call, raises, says):
    with pytest.raises(raises) as raised:
        call()
    assert type(raised.value) is raises
    if says is not None:
        assert str(raised.value) == says


class _UnresolvableHint:
    def __init__(self, x: "NotDefinedAnywhere"):  # noqa: F821
        self.x = x


def _autowired(provider):
    container = Container()
    container.register(provider, provider, autowire=True)
    container.resolve(provider)


# An error that replaces another keeps it as its cause, as Python's `raise X from e` does.
CAUSED = [
    # (call, raises, caused by)
    (_merge_sum_into_text, ArgumentError, TypeError),
    (lambda: _autowired(int), SignatureError, ValueError),
    (lambda: _autowired(_UnresolvableHint), SignatureError, NameError),
]


@pytest.mark.parametrize("call, raises, cause", CAUSED, ids=[f"{i:02}-{c[1].__name__}" for i, c in enumerate(CAUSED)])
def test_an_error_that_replaces_another_keeps_it_as_its_cause(call, raises, cause):
    with pytest.raises(raises) as raised:
        call()
    assert type(raised.value.__cause__) is cause


def test_an_error_core_code_nests_becomes_its_cause(tmp_path):
    bad = tmp_path / "bad.yaml"
    bad.write_text("a: [1, 2\n", encoding="utf-8")
    with pytest.raises(ParseError) as raised:
        path(bad).read()
    assert type(raised.value.__cause__) is ParseError
    assert str(raised.value) == f"{raised.value.__cause__} in {bad}"


# A missing attribute names the attribute and the object asked, as Python's AttributeError does, so
# a traceback can suggest the name that was meant.
MISSING = [
    # (call, name, the object asked)
    (lambda: each([1, 2]).reel, "reel", 1),
    (lambda: getattr(each([1, 2]), "__nope__"), "__nope__", None),
    (lambda: pygim.no_such_thing, "no_such_thing", pygim),
]


@pytest.mark.parametrize("call, name, asked", MISSING, ids=[m[1] for m in MISSING])
def test_a_missing_attribute_names_it_and_the_object_asked(call, name, asked):
    with pytest.raises(MissingAttributeError) as raised:
        call()
    assert raised.value.name == name
    if asked is not None:
        assert raised.value.obj is asked


def test_a_missing_attribute_gets_python_s_suggestion():
    with pytest.raises(MissingAttributeError) as raised:
        each([1, 2]).reel
    assert "Did you mean: 'real'?" in "".join(traceback.format_exception_only(raised.value))
