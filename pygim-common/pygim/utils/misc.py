# -*- coding: utf-8 -*-
"""
This module contains utilities not fitting to other category.
"""

__all__ = ["safedelattr"]


def safedelattr(obj, name):
    """Deletes attribute from the object and is happy if it is not there."""
    try:
        delattr(obj, name)
    except AttributeError:
        pass  # It is already deleted and we are fine with it.
