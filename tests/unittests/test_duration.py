# -*- coding: utf-8 -*-
"""pygim.utils.Duration and ManualTime: a length of time is one signed 64-bit count of nanoseconds;
every unit is a conversion into it."""

import math
from fractions import Fraction

import pytest

from pygim import utils

NS_PER = {
    "nsecs": 1,
    "usecs": 1_000,
    "msecs": 1_000_000,
    "secs": 1_000_000_000,
    "mins": 60_000_000_000,
    "hours": 3_600_000_000_000,
    "days": 86_400_000_000_000,
    "weeks": 604_800_000_000_000,
    "years": 31_556_952_000_000_000,   # C++'s year: 365.2425 days
}


@pytest.mark.parametrize("unit, ns", NS_PER.items(), ids=NS_PER.keys())
def test_one_of_each_unit_is_its_nanoseconds(unit, ns):
    assert int(utils.Duration(**{unit: 1})) == ns


def test_units_add_up():
    assert int(utils.Duration(mins=1, secs=30)) == 90_000_000_000


def test_units_past_their_range_carry_over():
    assert utils.Duration(mins=1, secs=90) == utils.Duration(mins=2, secs=30)


def test_no_unit_is_zero():
    assert int(utils.Duration()) == 0


def test_a_fraction_converts():
    assert int(utils.Duration(secs=1.5)) == 1_500_000_000


AMOUNTS = [k / 1000 for k in range(1, 300)] + [k / 7 for k in range(1, 2000)]


@pytest.mark.parametrize("unit", ["years", "days", "secs"])
def test_a_fraction_is_rounded_exactly(unit):
    """The float's exact value times the unit, rounded half to even, as Python's own exact arithmetic
    rounds it. A float product cannot: a year is 55 bits of nanoseconds, and for 1,722 of these 2,298
    amounts of a year round(amount * unit) misses by a nanosecond (years=0.286: ...272000000 against
    the exact ...271999999)."""
    wrong = [x for x in AMOUNTS if int(utils.Duration(**{unit: x})) != round(Fraction(x) * NS_PER[unit])]
    assert wrong == []


def test_any_real_number_converts():
    assert int(utils.Duration(msecs=Fraction(1, 4))) == 250_000


@pytest.mark.parametrize("amount, ns", [(0.5, 0), (1.5, 2), (2.5, 2), (-0.5, 0), (-1.5, -2)])
def test_a_fraction_of_a_nanosecond_rounds_to_even(amount, ns):
    assert int(utils.Duration(nsecs=amount)) == ns


def test_a_negative_amount_is_a_negative_duration():
    assert int(utils.Duration(secs=-5)) == -5_000_000_000


def test_past_64_bits_is_refused():
    with pytest.raises(OverflowError):
        utils.Duration(years=300)


def test_an_integer_past_64_bits_is_refused():
    with pytest.raises(OverflowError):
        utils.Duration(nsecs=2**63)


def test_a_sum_past_64_bits_is_refused():
    with pytest.raises(OverflowError):
        utils.Duration(years=200, weeks=5_000)


@pytest.mark.parametrize("amount", [math.nan, math.inf, -math.inf])
def test_a_non_finite_amount_is_refused(amount):
    with pytest.raises(ValueError):
        utils.Duration(secs=amount)


def test_an_unknown_unit_is_refused():
    with pytest.raises(TypeError, match="months"):
        utils.Duration(months=1)


class Index:
    """An integer that is not an int: what numpy's integers are to Python (they have __index__)."""

    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value


def test_an_index_integer_converts_exactly():
    """A numpy integer is an integer: taken whole, not through a float (2**60 + 1 has no float)."""
    assert int(utils.Duration(nsecs=Index(2**60 + 1))) == 2**60 + 1


def test_a_bool_is_refused():
    with pytest.raises(TypeError, match="bool"):
        utils.Duration(secs=True)


def test_a_non_number_is_refused():
    with pytest.raises(TypeError):
        utils.Duration(secs="1")


def test_seconds_is_a_view():
    assert utils.Duration(mins=2, secs=30).seconds == 150.0


def test_durations_order_and_hash_by_their_number():
    assert utils.Duration(secs=1) < utils.Duration(secs=2) and hash(utils.Duration(secs=150)) == hash(utils.Duration(mins=2, secs=30))


def test_repr_spells_the_exact_units():
    assert repr(utils.Duration(mins=1, secs=90)) == "Duration(mins=2, secs=30)"


def test_repr_round_trips():
    d = utils.Duration(years=1, nsecs=7)
    assert eval(repr(d), {"Duration": utils.Duration}) == d


def test_manual_time_starts_at_zero():
    assert utils.ManualTime().now() == 0


def test_manual_time_advances_by_units():
    time = utils.ManualTime()
    time.advance(mins=1, secs=30)
    time.advance(mins=1, secs=90)
    time.advance(nsecs=50)
    assert time.now() == 240_000_000_050


def test_manual_time_advances_by_a_duration():
    time = utils.ManualTime()
    time.advance(utils.Duration(years=1))
    assert time.now() == 31_556_952_000_000_000


def test_manual_time_refuses_to_pass_64_bits_and_does_not_move():
    time = utils.ManualTime()
    time.advance(years=200)
    with pytest.raises(OverflowError):
        time.advance(years=100)
    assert time.now() == 200 * NS_PER["years"]


def test_advance_takes_a_duration_or_units_not_both():
    with pytest.raises(TypeError, match="not both"):
        utils.ManualTime().advance(utils.Duration(secs=1), secs=1)


def test_advance_given_nothing_moves_nothing():
    time = utils.ManualTime()
    time.advance()
    assert time.now() == 0
