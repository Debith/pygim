# -*- coding: utf-8 -*-
#type: ignore
""" Test utility functions. """

from functools import singledispatch
import pytest

from pygim.iterables import flatten

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


if __name__ == "__main__":
    pytest.main([__file__])