# -*- coding: utf-8 -*-
"""
This module contains miscellenious utility functions that can't be fit anywhere else.
"""

import pygim.kernel.typing_ext as t
from typing import Iterable, Callable, Any

__all__ = ['split', 'is_container', 'flatten']


cpdef split(it: t.Iterable[Any], condition: t.Callable):
    """
    Split a iterable object into two, based on given condition.

    Args:
        it (Iterable[Any]): Iterator to be split in two.
        condition (Callable): Function decide the way of splitting.

    Returns:
        result (Tuple): Two lists where left side has values matching the
                        condition and right side has the rest.
    """

    cdef list left = []
    cdef list right = []

    for i in range(len(it)):
        if condition(it[i]):
            left.append(it[i])
        else:
            right.append(it[i])

    return left, right


cpdef is_container(obj: t.Any):
    """ Checks whether the object is container or not.

    Container is considered an object, which includes other objects,
    thus string is not qualified, even it implments iterator protocol.

    >>> is_container("text")
    False

    >>> is_container(tuple())
    True
    """
    if isinstance(obj, (str, bytes, type)):
        return False

    if hasattr(obj, '__iter__'):
        return True

    return isinstance(obj, memoryview)


cpdef flatten(items: t.Iterable[t.Any]):
    """ Flatten the nested arrays into single one.

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
    cdef list new_items = []

    for subitem in items:
        if is_container(subitem):
            for item in flatten(subitem):
                new_items.append(item)
        else:
            new_items.append(subitem)

    return new_items