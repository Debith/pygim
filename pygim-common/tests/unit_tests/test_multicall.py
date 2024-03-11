# -*- coding: utf-8 -*-
'''
This module implmements .
'''

import pytest
from pygim.testing import diff


@pytest.fixture
def multiline_string_list():
    return ["this   ", "  is   ", " a   ", " test   "]


def test_multicall_calling_function_of_an_underlying_objects(multiline_string_list):
    from _pygim.common_fast import MultiCall
    mgetattr = MultiCall([], "")

    actual_result = mgetattr(multiline_string_list, "strip")
    expected_result = ["this", "is", "a", "test"]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_calling_function_of_an_underlying_objects_with_args(multiline_string_list):
    from pygim.utils import mgetattr

    actual_result = mgetattr(multiline_string_list, "replace", args=(" ", ""))
    expected_result = ["this", "is", "a", "test"]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_does_not_call_functions_when_autocall_is_disabled(multiline_string_list):
    from pygim.utils import mgetattr

    actual_result = mgetattr(multiline_string_list, "strip", autocall=False)
    # The result should be a list of functions, not the results of calling them.
    expected_result = [o.strip for o in multiline_string_list]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_passing_ellipsis_returns_a_generator(multiline_string_list):
    from pygim.utils import mgetattr

    actual_result = mgetattr(multiline_string_list, "strip", ...)
    expected_result = (o.strip() for o in multiline_string_list)
    if not isinstance(actual_result, type(expected_result)):
        raise AssertionError(f"Expected a generator, got {type(actual_result)}")
    actual_result = list(actual_result)
    expected_result = list(expected_result)
    if actual_result != expected_result:
        raise AssertionError(diff(list(actual_result), list(expected_result)))


def test_multicall_passing_dict_to_factory_returns_a_dict(multiline_string_list):
    from pygim.utils import mgetattr

    actual_result = mgetattr(multiline_string_list, "strip", dict)
    expected_result = {o: o.strip() for o in multiline_string_list}
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_allows_accessing_attributes_of_objects():
    from pygim.utils import mgetattr
    from dataclasses import dataclass

    @dataclass
    class A:
        a: int

    objects = [A(1), A(2), A(3)]
    actual_result = mgetattr(objects, "a")
    expected_result = [1, 2, 3]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_passing_default_returns_default_when_attribute_not_found(multiline_string_list):
    from pygim.utils import mgetattr

    multiline_string_list.append(123)
    actual_result = mgetattr(multiline_string_list, "strip", default=None)
    expected_result = ["this", "is", "a", "test", None]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_passing_ellipsis_as_default_drops_items(multiline_string_list):
    from pygim.utils import mgetattr

    multiline_string_list.append(123)
    actual_result = mgetattr(multiline_string_list, "strip", default=...)
    expected_result = ["this", "is", "a", "test"]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def xtest_multicall_for_long_list():
    from pygim.utils import mgetattr
    from _pygim.common_fast import MultiCall
    mgetattr = MultiCall([], "", list)

    actual_result = mgetattr([1] * 1000000, "__str__")
    expected_result = [2] * 1000000
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))



if __name__ == '__main__':
    from pygim.testing import run_tests
    run_tests(__file__, None, coverage=False)