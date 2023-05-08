# -*- coding: utf-8 -*-
'''
Internal package for complaining functions.
'''

import types

__all__ = ('type_error_msg', 'TraitFunctions', 'has_instances', 'is_subset')

TraitFunctions = (types.FunctionType, types.MethodType)

def type_error_msg(obj, expected_type):
    """
    Returns a formatted error message for a type error.

    Parameters
    ----------
    obj : Any
        The object that was found to have a type error.
    expected_type : type or tuple of types
        The expected type(s) of the object.

    Returns
    -------
    str
        The formatted error message.

    Examples
    --------
    >>> type_error_msg(2, str)
    "Expected to get type `str`, got `2 [int]`"
    >>> type_error_msg([], (tuple, list))
    "Expected to get type `(tuple,list)`, got `[] [list]`"
    """
    if isinstance(expected_type, tuple):
        type_names = ",".join(f"`{t.__name__}`" for t in expected_type)
    else:
        type_names = type(obj).__name__
    return f"Expected to get type `{expected_type.__name__}`, got `{repr(obj)} [{type_names}]`"


def has_instances(iterable, types, *, how=all):
    """
    Check if all or any items in an iterable are instances of a specified type.

    Parameters
    ----------
    iterable : iterable
        The iterable to check.
    types : type or tuple of types
        The expected type(s) of the items.
    how : callable, optional
        A callable that will be used to aggregate the results of the checks
        (e.g. `all` to check if all items are instances of the specified type(s),
        `any` to check if any items are instances of the specified type(s)).
        Defaults to `all`.

    Returns
    -------
    bool
        True if all/any items in the iterable are instances of the specified type(s),
        False otherwise.

    Examples
    --------
    >>> has_instances([1,2,3], int)
    True
    >>> has_instances([1,2,'3'], int)
    False
    >>> has_instances([1,2,'3'], int, how=any)
    True
    """
    return how(isinstance(it, types) for it in iterable)


def is_subset(iterable, other):
    """
    Check if an iterable is a subset of another iterable.

    Parameters
    ----------
    iterable : iterable
        The iterable to check.
    other : iterable
        The iterable to check against.

    Returns
    -------
    bool
        True if `iterable` is a subset of `other`, False otherwise.

    Examples
    --------
    >>> is_subset([1, 2], [1, 2, 3])
    True
    >>> is_subset([1, 2, 3], [1, 2])
    False
    """
    return set(iterable).issubset(other)