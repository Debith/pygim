# -*- coding: utf-8 -*-
""" Test utility functions. """

from functools import singledispatch
import pytest

try:
    import pyximport; pyximport.install()
except ImportError:
    pass
from pygim.utils import flatten, is_container, split

@singledispatch
def equals(left: object, right):
    return left == right


FLATTEN_TESTS = [
    ([1, 2, 3],                                     [1, 2, 3]),
    ([[1], [2], [3]],                               [1, 2, 3]),
    ([[[[1]]], [[[2]]], [[[3]]]],                   [1, 2, 3]),
    (set([1, 2, 3]),                                [1, 2, 3]),
    ([set([1]), [[[2]]], 3],                        [1, 2, 3]),

    (["one", 2, 3],                                 ["one", 2, 3]),
    ([["one"], [2], [3]],                           ["one", 2, 3]),
    ([[[["one"]]], [[[2]]], [[[3]]]],               ["one", 2, 3]),
    (set(["one", 2, 3]),                            ["one", 2, 3]),
    ([set(["one"]), [[[2]]], 3],                    ["one", 2, 3]),

    (["one", "two", "three"],                       ["one", "two", "three"]),
    ([["one"], ["two"], ["three"]],                 ["one", "two", "three"]),
    ([[[["one"]]], [[["two"]]], [[["three"]]]],     ["one", "two", "three"]),
    (set(["one", "two", "three"]),                  ["one", "two", "three"]),
    ([set(["one"]), [[["two"]]], "three"],          ["one", "two", "three"]),

    ([str, None, False],                            [str, None, False]),
    ([[str], [None], [False]],                      [str, None, False]),
    ([[[[str]]], [[[None]]], [[[False]]]],          [str, None, False]),
    (set([str, None, False]),                       [str, None, False]),
    ([set([str]), [[[None]]], False],               [str, None, False]),
]

@pytest.mark.parametrize("input,expected_output", FLATTEN_TESTS)
def test_flatten(input, expected_output):
    expected_output = input.__class__(expected_output)
    actual_result = input.__class__(flatten(input))
    if not equals(actual_result, expected_output):
        assert False


IS_CONTAINER_TESTS = [
    (str, False),
    (bytes, False),
    (bytearray, False),
    (memoryview, False),
    (range, False),
    (list, False),
    (tuple, False),
    (int, False),
    (float, False),
    (complex, False),
    (set, False),
    (frozenset, False),
    (dict, False),

    # Various instances
    ('text', False),
    (b'text', False),
    (bytearray([1,2,3]), True),
    (memoryview(bytearray([1,2,3])), True),
    (range(100), True),
    ([1,2,3], True),
    ((1,2,3), True),
    (42, False),
    (42.42, False),
    (complex(42, 42), False),
    (set([1, 2, 3]), True),
    (frozenset([1, 2, 3]), True),
    (dict(one=1), True),
]

@pytest.mark.parametrize("input,expected_output", IS_CONTAINER_TESTS)
def test_is_container(input, expected_output):
    actual_result = is_container(input)
    if not equals(actual_result, expected_output):
        assert False, f"{type(input)} is not {expected_output}"


SPLIT_TESTS = [
    ([1, 2, 3, 4], lambda v: v % 2, ([1, 3], [2, 4])),
    ([1, 2, 3, 4], lambda v: v <= 2, ([1, 2], [3, 4])),
    ([], lambda v: v <= 2, ([], [])),
]

@pytest.mark.parametrize("input,func,expected_output", SPLIT_TESTS)
def test_split(input, func, expected_output):

    actual_result = split(input, func)
    if not equals(actual_result, expected_output):
        assert False, f"{actual_result} != {expected_output}"


if __name__ == "__main__":
    pytest.main([__file__])