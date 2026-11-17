# -*- coding: utf-8 -*-
"""Every failure pygim raises is a class of pygim.errors (docs/design/errors.md): one row per raise
site that Python can reach, the call first, then the class it raises and what it says. Each class is
also the builtin its meaning needs, so `except TypeError` and the like keep working; the rows check
pygim's class, which is the stronger claim."""

import math

import pytest

import pygim
from pygim import utils
from pygim.core.funcs import get_schema
from pygim.each import each
from pygim.errors import (
    ArgumentError,
    MissingAttributeError,
    GimOptionError,
    GimOverflowError,
    InvalidStateError,
    InvalidValueError,
    SignatureError,
    UnrecognizedTypeError,
)


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
    (lambda: utils.Duration(parsecs=1), ArgumentError, None),
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
