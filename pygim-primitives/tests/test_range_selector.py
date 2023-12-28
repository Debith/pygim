# -*- coding: utf-8 -*-
import re
import pytest
from pygim.explib import GimError
from pygim.primitives.range_selector import RangeSelector

_TUPLE_RANGES = [
    (-10, 0),
    (0, 10),
    (10, 20),
    (20, 30),
    (30, 40),
]

_DICT_RANGES = {
    (-10, 0): 'a',
    (0, 10): 'b',
    (10, 20): 'c',
    (20, 30): 'd',
    (30, 40): 'e',
}

_INT_RANGES = [-10, 0, 10]

@pytest.mark.parametrize("ranges, values, value, expected", [
    (_TUPLE_RANGES, None, 5, 1),
    (_INT_RANGES, ['a', 'b'], 2, 'b'),
    (_DICT_RANGES, None, 5, 'b')
])
def test_range_selector(ranges, values, value, expected):
    rs = RangeSelector(ranges, values)
    assert rs.select(value) == expected


@pytest.mark.parametrize("ranges, values, expected_length", [
    (_TUPLE_RANGES, None, 5),
    (_INT_RANGES, ['a', 'b'], 2),
    (_DICT_RANGES, None, 5)
])
def test_range_selector_length(ranges, values, expected_length):
    rs = RangeSelector(ranges, values)
    assert len(rs) == expected_length


@pytest.mark.parametrize("func, ranges, values, expected_repr", [
    (repr, _TUPLE_RANGES, None,
    "RangeSelector([(-10, 0), (0, 10), (10, 20), (20, 30), (30, 40)])"),
    (repr, _INT_RANGES, ['a', 'b'],
    "RangeSelector([(-10, 0), (0, 10)], ['a', 'b'])"),
    (repr, _DICT_RANGES, None,
    "RangeSelector([(-10, 0), (0, 10), (10, 20), (20, 30), (30, 40)], ['a', 'b', 'c', 'd', 'e'])"),
    (str, _TUPLE_RANGES, None,
     "-10  ->   0  :  0\n  0  ->  10  :  1\n 10  ->  20  :  2\n 20  ->  30  :  3\n 30  ->  40  :  4"),
    (str, _INT_RANGES, ['a', 'b'],
     "-10  ->   0  :  a\n  0  ->  10  :  b"),
    (str, _DICT_RANGES, None,
     "-10  ->   0  :  a\n  0  ->  10  :  b\n 10  ->  20  :  c\n 20  ->  30  :  d\n 30  ->  40  :  e")
])
def test_range_selector_repr(func, ranges, values, expected_repr):
    rs = RangeSelector(ranges, values)
    assert func(rs) == expected_repr


@pytest.mark.parametrize("ranges, value, expected", [
    (_TUPLE_RANGES, -5, True),
    (_TUPLE_RANGES, 5, True),
    (_TUPLE_RANGES, 15, True),
    (_TUPLE_RANGES, 25, True),
    (_TUPLE_RANGES, 35, True),
    (_TUPLE_RANGES, -15, False),
    (_TUPLE_RANGES, 45, False),
    (_DICT_RANGES, -5, True),
    (_DICT_RANGES, 5, True),
    (_DICT_RANGES, 15, True),
    (_DICT_RANGES, 25, True),
    (_DICT_RANGES, 35, True),
    (_DICT_RANGES, -15, False),
    (_DICT_RANGES, 45, False),
])
def test_range_selector_contains(ranges, value, expected):
    rs = RangeSelector(ranges)
    assert (value in rs) == expected


@pytest.mark.parametrize("ranges, index, expected", [
    (_TUPLE_RANGES, (-10, 0), 0),
    (_TUPLE_RANGES, (10, 20), 2),
    (_TUPLE_RANGES, (30, 40), 4),
    (_TUPLE_RANGES, 0, 1),
    (_TUPLE_RANGES, -1, 0),
    (_TUPLE_RANGES, 39, 4),
    (_TUPLE_RANGES, 30, 4),
    (_TUPLE_RANGES, slice(0, 20), [1, 2]),
    (_TUPLE_RANGES, slice(10, 40), [2, 3, 4]),
    (_INT_RANGES, 0, 1),
    (_INT_RANGES, -1, 0),
    (_INT_RANGES, 9, 1),
    (_DICT_RANGES, (-10, 0), 'a'),
    (_DICT_RANGES, (10, 20), 'c'),
    (_DICT_RANGES, (30, 40), 'e'),
    (_DICT_RANGES, 0, 'b'),
    (_DICT_RANGES, -1, 'a'),
    (_DICT_RANGES, 39, 'e'),
    (_DICT_RANGES, 30, 'e'),
    (_DICT_RANGES, slice(0, 20), ['b', 'c']),
    (_DICT_RANGES, slice(10, 40), ['c', 'd', 'e']),
])
def test_range_selector_getitem(ranges, index, expected):
    rs = RangeSelector(ranges)
    actual = rs[index]
    if actual != expected:
        raise AssertionError(f'{rs[index]} != {expected}')


@pytest.mark.parametrize("ranges, values, exception, message", [
    (None, None, GimError, 'Parameter ``ranges`` must be specified'),
    (True, None, GimError, re.escape('Expected to get type `Mapping,Sequence`, got `True [bool]`')),
])
def test_can_not_create_range_selector_with_no_ranges(ranges, values, exception, message):
    with pytest.raises(exception, match=message):
        RangeSelector(ranges, values)


def test_key_iterator():
    rs = RangeSelector(_TUPLE_RANGES)
    assert list(rs) == list(_TUPLE_RANGES)
    assert list(reversed(rs)) == list(reversed(_TUPLE_RANGES))


@pytest.mark.parametrize("value, expected", [
    (40,    'Value 40 is out of range of -10-40'),
    (-11,   'Value -11 is out of range of -10-40'),
])
def test_selecting_out_of_range(value, expected):
    rs = RangeSelector(_TUPLE_RANGES)
    with pytest.raises(GimError, match=expected):
        rs.select(value)


def test_super_long_range():
    rs = RangeSelector(list(range(0, 1000000, 5)))
    assert rs[1000] == 200
    assert rs[100:50000] == list(range(20, 10000, 5))


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, RangeSelector.__module__, coverage=False)
