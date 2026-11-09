# -*- coding: utf-8 -*-
"""pygim.Duration and ManualTime: a length of time is one signed 64-bit count of nanoseconds;
every unit is a conversion into it."""

import math
from fractions import Fraction

import pytest

import pygim

NS_PER = {
    "ns": 1,
    "us": 1_000,
    "ms": 1_000_000,
    "s": 1_000_000_000,
    "min": 60_000_000_000,
    "hr": 3_600_000_000_000,
    "day": 86_400_000_000_000,
    "wk": 604_800_000_000_000,
    "yr": 31_556_952_000_000_000,   # C++'s year: 365.2425 days
}


@pytest.mark.parametrize("unit, ns", NS_PER.items(), ids=NS_PER.keys())
def test_one_of_each_unit_is_its_nanoseconds(unit, ns):
    assert int(pygim.Duration(**{unit: 1})) == ns


def test_units_add_up():
    assert int(pygim.Duration(min=1, s=30)) == 90_000_000_000


def test_units_past_their_range_carry_over():
    assert pygim.Duration(min=1, s=90) == pygim.Duration(min=2, s=30)


def test_no_unit_is_zero():
    assert int(pygim.Duration()) == 0


def test_a_fraction_converts():
    assert int(pygim.Duration(s=1.5)) == 1_500_000_000


AMOUNTS = [k / 1000 for k in range(1, 300)] + [k / 7 for k in range(1, 2000)]


@pytest.mark.parametrize("unit", ["yr", "day", "s"])
def test_a_fraction_is_rounded_exactly(unit):
    """The float's exact value times the unit, rounded half to even, as Python's own exact arithmetic
    rounds it. A float product cannot: a year is 55 bits of nanoseconds, and for 1,722 of these 2,298
    amounts of a year round(amount * unit) misses by a nanosecond (yr=0.286: ...272000000 against
    the exact ...271999999)."""
    wrong = [x for x in AMOUNTS if int(pygim.Duration(**{unit: x})) != round(Fraction(x) * NS_PER[unit])]
    assert wrong == []


def test_any_real_number_converts():
    assert int(pygim.Duration(ms=Fraction(1, 4))) == 250_000


@pytest.mark.parametrize("amount, ns", [(0.5, 0), (1.5, 2), (2.5, 2), (-0.5, 0), (-1.5, -2)])
def test_a_fraction_of_a_nanosecond_rounds_to_even(amount, ns):
    assert int(pygim.Duration(ns=amount)) == ns


def test_a_negative_amount_is_a_negative_duration():
    assert int(pygim.Duration(s=-5)) == -5_000_000_000


def test_past_64_bits_is_refused():
    with pytest.raises(OverflowError):
        pygim.Duration(yr=300)


def test_an_integer_past_64_bits_is_refused():
    with pytest.raises(OverflowError):
        pygim.Duration(ns=2**63)


def test_a_sum_past_64_bits_is_refused():
    with pytest.raises(OverflowError):
        pygim.Duration(yr=200, wk=5_000)


@pytest.mark.parametrize("amount", [math.nan, math.inf, -math.inf])
def test_a_non_finite_amount_is_refused(amount):
    with pytest.raises(ValueError):
        pygim.Duration(s=amount)


def test_an_unknown_unit_is_refused():
    with pytest.raises(TypeError, match="months"):
        pygim.Duration(months=1)


@pytest.mark.parametrize("name", ["years", "mins", "secs", "msecs", "y", "m", "h", "d", "w", "sec"])
def test_units_are_the_named_ones_and_only_those(name):
    """yr, wk, day, hr, min, s, ms, us, ns, as Debith chose them to read (C++'s literals say the
    same: 1_min, 20_us). Any other spelling is refused, never a second name, and the refusal names
    the units to use."""
    with pytest.raises(TypeError, match=r"the units are yr, wk, day, hr, min, s, ms, us and ns$"):
        pygim.Duration(**{name: 1})


class Index:
    """An integer that is not an int: what numpy's integers are to Python (they have __index__)."""

    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value


def test_an_index_integer_converts_exactly():
    """A numpy integer is an integer: taken whole, not through a float (2**60 + 1 has no float)."""
    assert int(pygim.Duration(ns=Index(2**60 + 1))) == 2**60 + 1


def test_a_bool_is_refused():
    with pytest.raises(TypeError, match="bool"):
        pygim.Duration(s=True)


def test_a_non_number_is_refused():
    with pytest.raises(TypeError):
        pygim.Duration(s="1")


def test_seconds_is_a_view():
    assert pygim.Duration(min=2, s=30).seconds == 150.0


def test_durations_order_and_hash_by_their_number():
    assert pygim.Duration(s=1) < pygim.Duration(s=2) and hash(pygim.Duration(s=150)) == hash(pygim.Duration(min=2, s=30))


def test_repr_spells_the_exact_units():
    assert repr(pygim.Duration(min=1, s=90)) == "Duration(min=2, s=30)"


def test_repr_round_trips():
    d = pygim.Duration(yr=1, ns=7)
    assert eval(repr(d), {"Duration": pygim.Duration}) == d


def test_manual_time_starts_at_zero():
    assert pygim.ManualTime().now() == 0


def test_manual_time_advances_by_units():
    time = pygim.ManualTime()
    time.advance(min=1, s=30)
    time.advance(min=1, s=90)
    time.advance(ns=50)
    assert time.now() == 240_000_000_050


def test_manual_time_advances_by_a_duration():
    time = pygim.ManualTime()
    time.advance(pygim.Duration(yr=1))
    assert time.now() == 31_556_952_000_000_000


def test_manual_time_refuses_to_pass_64_bits_and_does_not_move():
    time = pygim.ManualTime()
    time.advance(yr=200)
    with pytest.raises(OverflowError):
        time.advance(yr=100)
    assert time.now() == 200 * NS_PER["yr"]


def test_advance_takes_a_duration_or_units_not_both():
    with pytest.raises(TypeError, match="not both"):
        pygim.ManualTime().advance(pygim.Duration(s=1), s=1)


def test_advance_given_nothing_moves_nothing():
    time = pygim.ManualTime()
    time.advance()
    assert time.now() == 0
