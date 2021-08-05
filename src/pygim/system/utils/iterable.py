"""
This module contains miscellenious utility functions that can't be fit anywhere else.
"""

from typing import Iterable, Callable, Any


def split(iterable: Iterable[Any], condition: Callable):
    """
    Split a iterable object into two, based on given condition.
    """
    left = []
    right = []

    for it in iterable:
        if condition(it):
            left.append(it)
        else:
            right.append(it)

    return left, right
