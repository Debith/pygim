# -*- coding: utf-8 -*-
"""
This module contains utilities to get profiling quickly.
"""

from contextlib import contextmanager
import cProfile
import pstats
import time
import pygim.typing as t

__all__ = ("quick_timer", "quick_profile")


@contextmanager
def quick_timer(title="Code block", *, printer=print):
    """
    Use this function to quickly measure time on a code block.

    Usage:
    '''python
        with quick_profile():
            slow_code()
    '''

    Arguments:
        title:      Print the time used to run code block.
    """
    start = time.time()
    yield
    end = time.time()
    printer(f"{title} executed in {end-start:.2f} seconds!")


@contextmanager
def quick_profile(top=30, *, sort="cumtime"):
    """
    Used to quickly print out profile results from the code inside the context.

    Usage:
    '''python
        with quick_profile():
            slow_code()
    '''

    Arguments:
        top:        Print the number of the slowest functions
        sort:       Sort functions from slowest to fastests using cumtime column
        examine:    Enables break points of the stats are printed for further debugging.
    """
    profile = cProfile.Profile()
    profile.enable()

    yield

    profile.disable()
    stats = pstats.Stats(profile).strip_dirs()
    stats.sort_stats(sort).print_stats(top)
