# -*- coding: utf-8 -*-
#type: ignore
""" Test utility functions. """

from functools import singledispatch
import pytest

from pygim.iterables import flatten, is_container, split

@singledispatch
def equals(left: object, right):
    return left == right

t = tuple

FLATTEN_TESTS = [
    ([],                                            []),
    ([1, 2, 3],                                     [1, 2, 3]),
    ([[1], [2], [3]],                               [1, 2, 3]),
    ([[[[1]]], [[[2]]], [[[3]]]],                   [1, 2, 3]),
    (set([1, 2, 3]),                                [1, 2, 3]),
    ([set([1]), [[[2]]], 3],                        [1, 2, 3]),
    (tuple([1, 2, 3]),                              [1, 2, 3]),
    ([1, [], 2],                                    [1, 2]),
    ((1,2,(3,4)),                                   [1,2,3,4]),
    ((1,2,set([3,4])),                              [1,2,3,4]),
    (range(10000),                                  list(range(10000))),
    ([[[[[[[[[[[]]]]]]]]]]],                        []),
    ([[[[[[[[[[[1]]]]]]]]]]],                       [1]),
    (set([t([t([t([])])])]),                        []),
    (set([t([t([t([1])])])]),                       [1]),

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
    (iter([1, 2, 3]),                               [1, 2, 3]),
    ((i for i in range(1, 4)),                      [1, 2, 3]),
    ({"a": 1},                                      [("a", 1)]),

    (["keep as is"],                                ["keep as is"]),
    ([b"keep as is"],                               [b"keep as is"]),
    (memoryview(b"keep as is"),                     [b"keep as is"]),
    (1,                                             [1]),
    (123.456,                                       [123.456]),
    (complex(1, 2),                                 [complex(1, 2)]),
    (None,                                          [None]),
    (True,                                          [True]),
]

@pytest.mark.parametrize("input,expected_result", FLATTEN_TESTS)
def test_flatten(input, expected_result):
    try:
        expected_result = input.__class__(expected_result)
    except (TypeError, ValueError):
        expected_result = list(expected_result)

    actual_result = flatten(input)
    try:
        actual_result = input.__class__(actual_result)
    except (TypeError, ValueError):
        actual_result = list(actual_result)
    if not equals(actual_result, expected_result):
        assert False, f"Results differ:\n  ACTUAL: {list(flatten(input))}\nEXPECTED: {expected_result} "


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

@pytest.mark.parametrize("input,expected_result", IS_CONTAINER_TESTS)
def test_is_container(input, expected_result):
    actual_result = is_container(input)
    if not equals(actual_result, expected_result):
        assert False, f"{type(input)} is not {expected_result}"


SPLIT_TESTS = [
    ([1, 2, 3, 4], lambda v: v % 2, ([1, 3], [2, 4])),
    ([1, 2, 3, 4], lambda v: v <= 2, ([1, 2], [3, 4])),
    ([], lambda v: v <= 2, ([], [])),
]

@pytest.mark.parametrize("input,func,expected_result", SPLIT_TESTS)
def test_split(input, func, expected_result):

    actual_result = split(input, func)
    if not equals(actual_result, expected_result):
        assert False, f"{actual_result} != {expected_result}"


if __name__ == "__main__":
    pytest.main([__file__])