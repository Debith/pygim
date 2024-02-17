# -*- coding: utf-8 -*-
"""
This module provides utilities for working with attributes.
"""

__all__ = ["safedelattr", "ggetattr"]

UNDEFINED = object()

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


def ggetattr(obj, name, *, autocall=True, default=UNDEFINED):
    """ Get attribute from the object and optionally call it.

    Parameters
    ----------
    obj : `object`
        Object containing the attribute.
    name : `str`
        Name of the attribute to be retrieved.
    autocall : `bool`, optional
        If `True`, and the attribute is callable, it will be called and the result
        will be returned. Defaults to `True`.
    default : `Any`, optional
        If the attribute is not found, this value will be returned. If not given,
        `AttributeError` will be raised.

    Returns
    -------
    `Any`
        The value of the attribute or the result of calling it if `autocall` is `True`.
    """
    if not hasattr(obj, name):
        if default is UNDEFINED:
            raise AttributeError(f"{obj!r} has no attribute {name!r}")
        return default
    value = getattr(obj, name)
    if autocall and callable(value):
        return value()
    return value
