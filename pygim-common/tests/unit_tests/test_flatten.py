# -*- coding: utf-8 -*-
#type: ignore
""" Test utility functions. """

from pathlib import Path
from functools import singledispatch
import pytest

from _pygim._iterlib import flatten

@singledispatch
def equals(left: object, right):
    return left == right

t = tuple

FLATTEN_TESTS = [
    ([],                                            []),
    ([1, 2, 3],                                     [1, 2, 3]),
    (list(range(10000)),                            list(range(10000))),
    ([[1], [2], [3]],                               [1, 2, 3]),
    ([[[[1]]], [[[2]]], [[[3]]]],                   [1, 2, 3]),
    (set([1, 2, 3]),                                [1, 2, 3]),
    ([set([1]), [[[2]]], 3],                        [1, 2, 3]),
    (tuple([1, 2, 3]),                              [1, 2, 3]),
    ([1, [], 2],                                    [1, 2]),
    ((1,2,(3,4)),                                   [1,2,3,4]),
    ((1,2,set([3,4])),                              [1,2,3,4]),
    (range(0),                                      list(range(0))),
    (range(1),                                      list(range(1))),
    (range(10000),                                  list(range(10000))),
    ([[[[[[[[[[[]]]]]]]]]]],                        []),
    ([[[[[[[[[[[1]]]]]]]]]]],                       [1]),
    (set([t([t([t([])])])]),                        []),
    (set([t([t([t([1])])])]),                       [1]),

    (["one", 2, 3],                                 ["one", 2, 3]),
    ([["one"], [2], [3]],                           ["one", 2, 3]),
    ([[[["one"]]], [[[2]]], [[[3]]]],               ["one", 2, 3]),
    (set(["one", 2, 3]),                            set(["one", 2, 3])),
    ([set(["one"]), [[[2]]], 3],                    set(["one", 2, 3])),

    (["one", "two", "three"],                       ["one", "two", "three"]),
    ([["one"], ["two"], ["three"]],                 ["one", "two", "three"]),
    ([[[["one"]]], [[["two"]]], [[["three"]]]],     ["one", "two", "three"]),
    (set(["one", "two", "three"]),                  set(["one", "two", "three"])),
    ([set(["one"]), [[["two"]]], "three"],          set(["one", "two", "three"])),

    ([str, None, False],                            [str, None, False]),
    ([[str], [None], [False]],                      [str, None, False]),
    ([[[[str]]], [[[None]]], [[[False]]]],          [str, None, False]),
    (set([str, None, False]),                       set([str, None, False])),
    ([set([str]), [[[None]]], False],               set([str, None, False])),
    (iter([1, 2, 3]),                               [1, 2, 3]),
    ((i for i in range(1, 4)),                      [1, 2, 3]),
    ({"a": 1},                                      ["a"]),

    (["keep as is"],                                ["keep as is"]),
    ([b"keep as is"],                               [b"keep as is"]),
    (memoryview(b"keep as is"),                     [b"keep as is"]),
    (1,                                             [1]),
    (123.456,                                       [123.456]),
    (complex(1, 2),                                 [complex(1, 2)]),
    (None,                                          [None]),
    (True,                                          [True]),
    ([Path.home(), Path.home(), Path.home()],       [Path.home(), Path.home(), Path.home()]),
]

@pytest.mark.parametrize("input,expected_result", FLATTEN_TESTS)
def test_flatten(input, expected_result):
    _factory_func = expected_result.__class__
    expected_result = _factory_func(expected_result)
    actual_result = _factory_func(flatten(input))

    if not equals(actual_result, expected_result):
        assert False, f"Results differ for {input}:\n  ACTUAL: {list(flatten(input))}\nEXPECTED: {expected_result} "


if __name__ == "__main__":
    pytest.main([__file__])