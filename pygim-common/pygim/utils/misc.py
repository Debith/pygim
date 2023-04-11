# -*- coding: utf-8 -*-
"""
This module contains utilities not fitting to other category.
"""

from dataclasses import MISSING

__all__ = ["safedelattr", "mgetattr"]


def safedelattr(obj, name):
    """Deletes attribute from the object and is happy if it is not there.

    Parameters
    ----------
    obj : `object`
        Object containing the attribute.
    name : `str`
        Name of the attribute to be deleted.
    """

    try:
        delattr(obj, name)
    except AttributeError:
        pass  # It is already deleted and we are fine with it.


def mgetattr(obj, name, *, default=MISSING):
    """
    Retrieve an attribute from an object given its name, which can be dotted to traverse
    nested attributes.

    Parameters
    ----------
    obj : `object`
        The object from which to retrieve the attribute.
    name : `str`
        The name of the attribute to retrieve. If the name contains dots, it will traverse nested attributes.
    default : `object`, optional
        The value to return if the attribute is not found. Defaults to `MISSING`.

    Returns
    -------
    out : object
        The value of the attribute, or the `default` value if the attribute is not found.

    Examples
    --------
    >>> class InnerClass:
    ...     def __init__(self):
    ...         self.foo = 'bar'
    ...
    >>> class MyClass:
    ...     def __init__(self):
    ...         self.inner = InnerClass()
    ...
    >>> my_object = MyClass()
    >>> result = mgetattr(my_object, 'inner.foo', default=None)
    >>> print(result)
    'bar'
    """
    assert name and isinstance(name, str)

    for part in name.split('.'):
        try:
            obj = getattr(obj, part)
        except AttributeError:
            if default is MISSING:
                raise
            obj = default
            break
    return obj


class class_or_instance_method:
    def __init__(self, func):
        self._func = func

    def __get__(self, _instance, _class):
        if _instance:
            return self._func.__get__(_instance, _class)    # invoke as method
        return self._func.__get__(_class, _class)           # invoke as classmethod
