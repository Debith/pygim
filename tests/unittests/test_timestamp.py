# -*- coding: utf-8 -*-
"""pygim.utils.Timestamp: a moment as nanoseconds since the Unix epoch, read from the wall clock."""

import time

from pygim import utils

SECOND = 1_000_000_000


def test_a_timestamp_is_its_nanoseconds():
    assert int(utils.Timestamp(12333)) == 12333


def test_now_reads_the_wall_clock():
    """Nanoseconds since 1970, as time.time_ns() counts them (a coarser Windows clock can trail by a tick)."""
    before = time.time_ns()
    now = int(utils.Timestamp.now())
    after = time.time_ns()
    assert before - SECOND <= now <= after + SECOND


def test_later_minus_earlier_is_a_duration():
    assert utils.Timestamp(3_500) - utils.Timestamp(1_000) == utils.Duration(nsecs=2_500)


def test_timestamps_order_by_their_moment():
    assert utils.Timestamp(1_000) < utils.Timestamp(3_500)


def test_equal_moments_are_equal_and_hash_alike():
    assert utils.Timestamp(7) == utils.Timestamp(7) and hash(utils.Timestamp(7)) == hash(utils.Timestamp(7))


def test_repr_names_the_moment():
    assert repr(utils.Timestamp(12333)) == "Timestamp(12333)"
