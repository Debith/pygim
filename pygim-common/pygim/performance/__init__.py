# -*- coding: utf-8 -*-
"""
Performance Utilities

This module provides utilities for performance measurement and optimization.

Functions
---------
quick_timer(func)
    Measure the execution time of a function and print the result.

quick_profile(func)
    Profile the execution of a function and print the profiling result.

dispatch(func)
    A function decorator that enhances function dispatch and performance.

"""

from .dispatch import *
from .timing_and_profiling import *

__all__ = [
    "quick_timer",
    "quick_profile",
    "dispatch",
]