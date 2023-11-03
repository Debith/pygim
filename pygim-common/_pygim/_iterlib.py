# -*- coding: utf-8 -*-
"""
This module contains internal utility functions.
"""

import itertools
from pathlib import Path

__all__ = ("split", "flatten", "is_container", "chunks", "dictify", "tuplify")


def split(iterable, condition):
    """
    Split an iterable object into two lists based on a given condition.

    Parameters
    ----------
    iterable : `iterable`
        Any iterable that needs to be split in two.
    condition : `callable`
        A function that takes a simple argument and returns a boolean value.
        The argument is used to decide which list the item should go into.

    Returns
    -------
    `tuple` [`list` `list`]
        A tuple containing two lists. The first list contains items that satisfy
        the condition, while the second list contains the remaining items.

    Notes
    -----
    The input iterable can be any iterable object such as string, tuple, list, set,
    or generator.

    Examples
    --------
    >>> numbers = [1, 2, 3, 4, 5]
    >>> def is_even(n):
    ...     return n % 2 == 0
    ...
    >>> even_numbers, odd_numbers = split(numbers, is_even)
    >>> even_numbers
    [2, 4]
    >>> odd_numbers
    [1, 3, 5]
    """
    left = []
    right = []

    for it in iterable:
        if condition(it):
            left.append(it)
        else:
            right.append(it)

    return left, right


def tuplify(obj):
    if isinstance(obj, dict):
        return tuple((k, v) for k, v in obj.items())
    if is_container(obj):
        return tuple(list(obj))
    return obj,


def is_container(obj):
    if isinstance(obj, (str, bytes, type, Path, memoryview)):
        return False

    if hasattr(obj, "__iter__"):
        return True

    return False


def flatten(iterable):
    if is_container(iterable):
        for o in iterable:
            yield from flatten(o)
    else:
        yield iterable


def chunks(iterable, size):
    it = iter(iterable)
    chunk = tuple(itertools.islice(it,size))
    while chunk:
        yield chunk
        chunk = tuple(itertools.islice(it,size))


def dictify(obj):
    if isinstance(obj, dict):
        return obj
    if isinstance(obj, tuple):
        return dict(chunks(obj, 2))
    raise NotImplementedError(f"Cannot convert {type(obj).__name__} to dict.")