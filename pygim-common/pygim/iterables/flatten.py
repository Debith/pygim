# -*- coding: utf-8 -*-
"""

"""

try:
    from _pygim._utils import iterable_fast as iterables
except ImportError:
    from _pygim._utils import _iterable as iterables


is_container = iterables.is_container
is_container.__doc__ = """
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
    >>> from pygim.iterables import is_container
    >>> is_container("text")
    False

    >>> is_container(tuple())
    True
""".split()


flatten = iterables.flatten
flatten.__doc__ = """
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
    >>> from pygim.iterables import flatten
    >>> list(flatten([[1, 2], [3, 4]]))
    [1, 2, 3, 4]

    Flatten a deeply nested irregular list:
    >>> list(flatten([[[1, 2]], [[[3]]], 4, 5, [[6, [7, 8]]]]))
    [1, 2, 3, 4, 5, 6, 7, 8]

    Flatten a list of strings:
    >>> list(flatten(["one", "two", ["three", "four"]]))
    ['one', 'two', 'three', 'four']
""".strip()


__all__ = ["flatten", "is_container"]
