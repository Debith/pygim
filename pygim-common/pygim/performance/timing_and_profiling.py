# -*- coding: utf-8 -*-
"""
This module contains utilities to get profiling quickly.
"""

from contextlib import contextmanager
import cProfile
import pstats
import time

__all__ = ("quick_timer", "quick_profile")


@contextmanager
def quick_timer(title = "Code block", *, printer=print):
    """
    Measure the execution time of a code block using a context manager.

    Parameters:
    -----------
    title : `str`, optional
        The title to display when printing the execution time. Default is "Code block".
    printer : `Callable[[str], None]`, optional
        The function to use for printing the execution time. Default is `print`.

    Yields:
    -------
    None
        This function is used as a context manager and doesn't return any values.

    Example:
    --------
    Measure the time it takes to execute a loop:
    .. code-block:: python

        from time import sleep

        def slow_code():
            for i in range(10):
                sleep(0.1)

        with quick_timer(title="Slow code"):
            slow_code()
    """
    start = time.time()
    yield
    end = time.time()
    printer(f"{title} executed in {end - start:.2f} seconds!")


@contextmanager
def quick_profile(top=30, *, sort="cumtime", examine=False):
    """
    Print profile results for code executed within a context.

    Parameters
    ----------
    top : int, optional
        Number of functions to print profiling results for. Default is 30.
    sort : str, optional
        Column to sort the profiling results by. Default is "cumtime".
    examine : bool, optional
        Flag indicating whether to enable breakpoints for further debugging. Default is False.

    Yields
    ------
    None
        This function is used as a context manager and doesn't return any values.

    Example
    -------
    Profile the execution time of a slow loop:
    ```python
    def slow_code():
        for i in range(100000):
            pass

    with quick_profile(top=5, sort="time"):
        slow_code()
    ```
    """
    profile = cProfile.Profile()
    profile.enable()

    try:
        yield
    finally:
        profile.disable()
        stats = pstats.Stats(profile).strip_dirs()
        stats.sort_stats(sort)
        if examine:
            stats.print_stats()
            breakpoint()
        else:
            stats.print_stats(top)
