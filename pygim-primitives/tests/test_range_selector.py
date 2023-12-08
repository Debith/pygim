# -*- coding: utf-8 -*-
import pytest
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


@pytest.mark.parametrize("ranges, values, expected_repr", [
    (_TUPLE_RANGES, None, "RangeSelector([(-10, 0), (0, 10), (10, 20), (20, 30), (30, 40)])"),
    (_INT_RANGES, ['a', 'b'], "RangeSelector([(-10, 0), (0, 10)], ['a', 'b'])"),
    (_DICT_RANGES, None, "RangeSelector([(-10, 0), (0, 10), (10, 20), (20, 30), (30, 40)], ['a', 'b', 'c', 'd', 'e'])")
])
def test_range_selector_repr(ranges, values, expected_repr):
    rs = RangeSelector(ranges, values)
    assert repr(rs) == expected_repr


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
    (_TUPLE_RANGES, 0, (-10, 0)),
    (_TUPLE_RANGES, 2, (10, 20)),
    (_TUPLE_RANGES, 4, (30, 40)),
    (_TUPLE_RANGES, -1, (30, 40)),
    (_INT_RANGES, 1, (0, 10)),
    (_INT_RANGES, -1, (0, 10)),
    (_TUPLE_RANGES, (-10, 0), 'a'),
    (_TUPLE_RANGES, (10, 20), 'c'),
    (_TUPLE_RANGES, (30, 40), 'e'),
    (_TUPLE_RANGES, 0, (-10, 0)),
    (_TUPLE_RANGES, 2, (10, 20)),
    (_TUPLE_RANGES, 4, (30, 40)),
    (_TUPLE_RANGES, -1, (30, 40)),
])
def test_range_selector_getitem(ranges, index, expected):
    rs = RangeSelector(ranges)
    actual = rs[index]
    if  actual != expected:
        raise AssertionError(f'{rs[index]} != {expected}')


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, RangeSelector.__module__, coverage=False)
