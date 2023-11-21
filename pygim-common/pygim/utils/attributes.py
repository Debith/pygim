# -*- coding: utf-8 -*-
"""
This module provides utilities for working with attributes.
"""

__all__ = ["safedelattr"]


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
