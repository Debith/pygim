# -*- coding: utf-8 -*-
"""
This module contains security utilities.
"""

import hashlib
from functools import singledispatch

import pygim.typing as t


__all__ = ["sha256sum"]


@singledispatch
def sha256sum(obj, *, encoding="utf-8"):
    """Quickly get SHA256 sum for given string.

    >>> sha256sum("hello sha256!")
    '705cb95c164e32feec2aef56f70d73e064afe2e38d40e5189fc5f8cdc84a9eaf'

    Args:
        obj (str):      String to be encoded.
        encoding (str): Encoding used to convert string objects into bytes.

    Returns:
        Calculated SHA256 sum
    """
    raise NotImplementedError(f"sha256sum not implemented for type: {type(obj)}")


@sha256sum.register(str)
def _(text: str, *, encoding="utf-8"):
    assert isinstance(text, str)
    return hashlib.sha256(text.encode(encoding)).hexdigest()


@sha256sum.register(bytes)
def _(text: bytes, **_):  # type: ignore
    assert isinstance(text, bytes)
    return hashlib.sha256(text).hexdigest()
