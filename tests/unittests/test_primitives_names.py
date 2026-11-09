# -*- coding: utf-8 -*-
"""Time and timing are primitives (core/primitives/): building blocks with a home of their own, not
utilities. Their Python names are pygim's own, ``from pygim import Duration``, made by the shared
object that binds them and exported lazily, as every top-level name is."""

import pytest

import pygim

NAMES = ["Duration", "Timestamp", "ManualTime", "QuickTimer", "Report", "TimerLap", "TimerSnapshot"]


@pytest.mark.parametrize("name", NAMES)
def test_a_time_primitive_is_a_top_level_name(name):
    assert name in pygim.__all__
    assert getattr(pygim, name).__name__ == name


@pytest.mark.parametrize("name", NAMES)
def test_a_time_primitive_names_pygim_as_its_home(name):
    """repr(type), help() and an error's traceback show pygim.Duration, never the shared object."""
    assert getattr(pygim, name).__module__ == "pygim"
