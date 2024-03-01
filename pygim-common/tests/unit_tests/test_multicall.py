# -*- coding: utf-8 -*-
'''
This module implmements .
'''

from pygim.testing import diff


def test_multicall_calling_function_of_an_underlying_objects():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   "]
    actual_result = mgetattr(objects, "strip")
    expected_result = ["this", "is", "a", "test"]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_calling_function_of_an_underlying_objects_with_args():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   "]
    actual_result = mgetattr(objects, "replace", args=(" ", ""))
    expected_result = ["this", "is", "a", "test"]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_does_not_call_functions_when_autocall_is_disabled():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   "]
    actual_result = mgetattr(objects, "strip", autocall=False)
    # The result should be a list of functions, not the results of calling them.
    expected_result = [o.strip for o in objects]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_passing_ellipsis_returns_a_generator():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   "]
    actual_result = mgetattr(objects, "strip", ...)
    expected_result = (o.strip() for o in objects)
    if not isinstance(actual_result, type(expected_result)):
        raise AssertionError(f"Expected a generator, got {type(actual_result)}")
    actual_result = list(actual_result)
    expected_result = list(expected_result)
    if actual_result != expected_result:
        raise AssertionError(diff(list(actual_result), list(expected_result)))


def test_multicall_passing_dict_to_factory_returns_a_dict():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   "]
    actual_result = mgetattr(objects, "strip", dict)
    expected_result = {o: o.strip() for o in objects}
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


def test_multicall_passing_default_returns_default_when_attribute_not_found():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   ", 123]
    actual_result = mgetattr(objects, "strip", default=None)
    expected_result = ["this", "is", "a", "test", None]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


def test_multicall_passing_ellipsis_as_default_drops_items():
    from pygim.utils import mgetattr

    objects = ["this   ", "  is   ", " a   ", " test   ", 123]
    actual_result = mgetattr(objects, "strip", default=...)
    expected_result = ["this", "is", "a", "test"]
    if actual_result != expected_result:
        raise AssertionError(diff(actual_result, expected_result))


if __name__ == '__main__':
    from pygim.testing import run_tests
    run_tests(__file__, None, coverage=False)