# -*- coding: utf-8 -*-
"""
This module provides utilities for working with attributes.
"""

from _pygim._utils import _field_explorer

__all__ = ["safedelattr", "smart_getattr", "mgetattr"]


safe_delattr = _field_explorer.safedelattr
safe_delattr.__doc__ = """
    Deletes attribute from the object and is happy if it is not there.

    Parameters
    ----------
    obj : `object`
        Object containing the attribute.
    name : `str`
        Name of the attribute to be deleted.
""".strip()

smart_getattr = _field_explorer.smart_getattr
smart_getattr.__doc__ = """
    Get attribute from the object and optionally call it.

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
    """.strip()

mgetattr = _field_explorer.mgetattr
