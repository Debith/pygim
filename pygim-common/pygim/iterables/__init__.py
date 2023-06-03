# -*- coding: utf-8 -*-
"""
Tools to iterate conviniently.
"""

from _pygim._utils._iterable import flatten, is_container, split

__all__ = [
    "flatten",                  # Convert nested arrays in to a one flat array.
    "is_container",             # Check whether object is iterable but not string or bytes.
    "split",                    # Split iterable in two iterables based on condition function.
]