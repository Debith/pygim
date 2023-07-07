# -*- coding: utf-8 -*-
#type: ignore
""" Test utility functions. """

from pathlib import Path
from functools import singledispatch
import pytest

from _pygim._iterlib import flatten
from _pygim.common_fast import flatten as flatten_fast



@singledispatch
def equals(left: object, right):
    return left == right

t = tuple

FLATTEN_TESTS = [
    #([],                                            []),
    #([1, 2, 3],                                     [1, 2, 3]),
    #([[1], [2], [3]],                               [1, 2, 3]),
    #([[[[1]]], [[[2]]], [[[3]]]],                   [1, 2, 3]),
    #(set([1, 2, 3]),                                [1, 2, 3]),
    #([set([1]), [[[2]]], 3],                        [1, 2, 3]),
    #(tuple([1, 2, 3]),                              [1, 2, 3]),
    #([1, [], 2],                                    [1, 2]),
    #((1,2,(3,4)),                                   [1,2,3,4]),
    #((1,2,set([3,4])),                              [1,2,3,4]),
    (range(10000),                                  list(range(10000))),
    #([[[[[[[[[[[]]]]]]]]]]],                        []),
    #([[[[[[[[[[[1]]]]]]]]]]],                       [1]),
    #(set([t([t([t([])])])]),                        []),
    #(set([t([t([t([1])])])]),                       [1]),

    #(["one", 2, 3],                                 ["one", 2, 3]),
    # ([["one"], [2], [3]],                           ["one", 2, 3]),
    # ([[[["one"]]], [[[2]]], [[[3]]]],               ["one", 2, 3]),
    # (set(["one", 2, 3]),                            ["one", 2, 3]),
    # ([set(["one"]), [[[2]]], 3],                    ["one", 2, 3]),

    # (["one", "two", "three"],                       ["one", "two", "three"]),
    # ([["one"], ["two"], ["three"]],                 ["one", "two", "three"]),
    # ([[[["one"]]], [[["two"]]], [[["three"]]]],     ["one", "two", "three"]),
    # (set(["one", "two", "three"]),                  ["one", "two", "three"]),
    # ([set(["one"]), [[["two"]]], "three"],          ["one", "two", "three"]),

    # ([str, None, False],                            [str, None, False]),
    # ([[str], [None], [False]],                      [str, None, False]),
    # ([[[[str]]], [[[None]]], [[[False]]]],          [str, None, False]),
    # (set([str, None, False]),                       [str, None, False]),
    # ([set([str]), [[[None]]], False],               [str, None, False]),
    # (iter([1, 2, 3]),                               [1, 2, 3]),
    # ((i for i in range(1, 4)),                      [1, 2, 3]),
    # ({"a": 1},                                      [("a", 1)]),

    # (["keep as is"],                                ["keep as is"]),
    # ([b"keep as is"],                               [b"keep as is"]),
    # (memoryview(b"keep as is"),                     [b"keep as is"]),
    # (1,                                             [1]),
    # (123.456,                                       [123.456]),
    # (complex(1, 2),                                 [complex(1, 2)]),
    # (None,                                          [None]),
    # (True,                                          [True]),
    # ([Path.home(), Path.home(), Path.home()],       [Path.home(), Path.home(), Path.home()]),
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


@pytest.mark.parametrize("input,expected_result", FLATTEN_TESTS)
def test_flatten_fast(input, expected_result):
    try:
        expected_result = input.__class__(expected_result)
    except (TypeError, ValueError):
        expected_result = list(expected_result)

    actual_result = flatten_fast(input)
    try:
        actual_result = input.__class__(actual_result)
    except (TypeError, ValueError):
        actual_result = list(actual_result)
    if not equals(actual_result, expected_result):
        assert False, f"Results differ:\n  ACTUAL: {list(flatten_fast(input))}\nEXPECTED: {expected_result} "

if __name__ == "__main__":
    pytest.main([__file__, "--capture=no"])