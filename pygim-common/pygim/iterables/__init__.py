# -*- coding: utf-8 -*-
"""
Iterable Utilities

This module provides utilities for working with iterables.

Functions
---------
flatten(iterable)
    Convert nested arrays into a single flat array.

is_container(obj)
    Check whether an object is iterable but not a string or bytes.

split(iterable, condition)
    Split an iterable into two iterables based on a condition function.

"""

from _pygim._utils._iterable import flatten, is_container, split

__all__ = [
    "flatten",
    "is_container",
    "split",
]