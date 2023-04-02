# -*- coding: utf-8 -*-
"""
This module contains miscellenious utility functions that can't be fit anywhere else.
"""

import pygim.typing as t

__all__ = ("split", "flatten", "is_container")


def split(iterable, condition):
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


def is_container(obj):
    """Checks whether the object is container or not.

    Container is considered an object, which includes other objects,
    thus string is not qualified, even it implments iterator protocol.

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


def flatten(items):
    """Flatten the nested arrays into single one.

    Example about list of lists.
    >>> list(flatten([[1, 2], [3, 4]]))
    [1, 2, 3, 4]

    Example of deeply nested irregular list:
    >>> list(flatten([[[1, 2]], [[[3]]], 4, 5, [[6, [7, 8]]]]))
    [1, 2, 3, 4, 5, 6, 7, 8]

    List of strings is handled properly too
    >>> list(flatten(["one", "two", ["three", "four"]]))
    ['one', 'two', 'three', 'four']
    """
    for subitem in items:
        if is_container(subitem):
            for item in flatten(subitem):
                yield item
        else:
            yield subitem
