"""
Check Mate: Type and Subset Checking Utilities

This module provides utility functions for checking instances and subsets.

Functions
---------
has_instances(obj, types)
    Check if the given object has an instance of any of the specified types.

is_subset(set1, set2)
    Check if `set1` is a subset of `set2`.
"""

from _pygim._utils import _inspect

__all__ = ('has_instances', 'is_subset')

has_instances = _inspect.has_instances
has_instances.__doc__ = """
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


is_subset = _inspect.is_subset
is_subset.__doc__ = """
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