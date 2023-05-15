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
    """
    Determine whether an object is a container.

    A container is considered an object that contains other objects. This
    function returns `False` for strings, bytes, and types, even though they
    implement the iterator protocol.

    Parameters
    ----------
    obj : `object`
        The object to check.

    Returns
    -------
    `bool`
        `True` if `obj` is a container, `False` otherwise.

    Examples
    --------
    >>> is_container("text")
    False

    >>> is_container(tuple())
    True
    """
    if isinstance(obj, (str, bytes, type)):
        return False

    if hasattr(obj, "__iter__"):
        return True

    return isinstance(obj, memoryview)


def flatten(iterable):
    """
    Flatten a nested iterable into a single list.

    This function flattens nested iterables such as lists, tuples, and sets
    into a single list. It can handle deeply nested and irregular structures.

    Parameters
    ----------
    iterable : `iterable`
        The nested iterable to flatten.

    Yields
    ------
    `object`
        The flattened objects from the nested iterable.

    Examples
    --------
    Flatten a list of lists:
    >>> list(flatten([[1, 2], [3, 4]]))
    [1, 2, 3, 4]

    Flatten a deeply nested irregular list:
    >>> list(flatten([[[1, 2]], [[[3]]], 4, 5, [[6, [7, 8]]]]))
    [1, 2, 3, 4, 5, 6, 7, 8]

    Flatten a list of strings:
    >>> list(flatten(["one", "two", ["three", "four"]]))
    ['one', 'two', 'three', 'four']
    """
    for subitem in iterable:
        if is_container(subitem):
            yield from flatten(subitem)
        else:
            yield subitem
