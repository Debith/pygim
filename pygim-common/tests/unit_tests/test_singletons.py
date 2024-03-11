# -*- coding: utf-8 -*-
import pytest

from _pygim.common_fast import Undefined

UNDEFINED = Undefined()

@pytest.mark.parametrize("left, right, expected_result", [
    (UNDEFINED, UNDEFINED, True),
    (None, UNDEFINED, False),
    (Undefined(), Undefined(), True),
    (UNDEFINED, None, False),
    (UNDEFINED, 1, False),
    (1, UNDEFINED, False),
    (UNDEFINED, 0, False),
    (0, UNDEFINED, False),
    (UNDEFINED, "", False),
    ("", UNDEFINED, False),
    (UNDEFINED, "a", False),
    ("a", UNDEFINED, False),
    (UNDEFINED, [], False),
    ([], UNDEFINED, False),
    (UNDEFINED, [1], False),
    ([1], UNDEFINED, False),
    (UNDEFINED, {}, False),
    ({}, UNDEFINED, False),
    (UNDEFINED, {"a": 1}, False),
    ({"a": 1}, UNDEFINED, False),
    (UNDEFINED, set(), False),
    (set(), UNDEFINED, False),
    (UNDEFINED, {1}, False),
    ({1}, UNDEFINED, False),
    (UNDEFINED, True, False),
    (True, UNDEFINED, False),
    (UNDEFINED, False, False),
    (False, UNDEFINED, False),
    (UNDEFINED, object(), False),
    (object(), UNDEFINED, False),
    (UNDEFINED, type("UNDEFINED", (object,), {}), False),
    (type("UNDEFINED", (object,), {}), UNDEFINED, False),
    (UNDEFINED, ..., False),
    (..., UNDEFINED, False),
])
def test_undefined(left, right, expected_result):
    if (left == right) != expected_result:
        raise AssertionError(f"Expected {left} == {right}")


@pytest.mark.parametrize("func, expected_result", [
    #(UNDEFINED.__bool__, False),
    (UNDEFINED.__repr__, "<UNDEFINED>"),
    #(UNDEFINED.__str__, "UNDEFINED"),
])
def test_representations(func, expected_result):
    assert func() == expected_result


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, None, coverage=False)
