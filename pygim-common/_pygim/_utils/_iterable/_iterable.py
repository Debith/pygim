# -*- coding: utf-8 -*-
"""
This module contains internal utility functions.
"""

__all__ = ("split", "flatten", "is_container")


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
    >>> even_numbers, odd_numbers = split_iterable(numbers, is_even)
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


def is_container(obj):

    if isinstance(obj, (str, bytes, type)):
        return False

    if hasattr(obj, "__iter__"):
        return True

    return isinstance(obj, memoryview)


def flatten(iterable):

    for subitem in iterable:
        if is_container(subitem):
            yield from flatten(subitem)
        else:
            yield subitem