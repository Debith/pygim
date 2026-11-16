# -*- coding: utf-8 -*-
"""pygim.Timestamp: a moment as nanoseconds since the Unix epoch, read from the wall clock."""

import time

import pygim

SECOND = 1_000_000_000


def test_a_timestamp_is_its_nanoseconds():
    assert int(pygim.Timestamp(12333)) == 12333


def test_now_reads_the_wall_clock():
    """Nanoseconds since 1970, as time.time_ns() counts them (a coarser Windows clock can trail by a tick)."""
    before = time.time_ns()
    now = int(pygim.Timestamp.now())
    after = time.time_ns()
    assert before - SECOND <= now <= after + SECOND


def test_later_minus_earlier_is_a_duration():
    assert pygim.Timestamp(3_500) - pygim.Timestamp(1_000) == pygim.Duration(ns=2_500)


def test_timestamps_order_by_their_moment():
    assert pygim.Timestamp(1_000) < pygim.Timestamp(3_500)


def test_equal_moments_are_equal_and_hash_alike():
    assert pygim.Timestamp(7) == pygim.Timestamp(7) and hash(pygim.Timestamp(7)) == hash(pygim.Timestamp(7))


def test_repr_names_the_moment():
    assert repr(pygim.Timestamp(12333)) == "Timestamp(12333)"
