# -*- coding: utf-8 -*-
"""
"""

import hashlib
import typing as t
from functools import singledispatch


__all__ = ['sha256sum']


@singledispatch
def sha256sum(obj: t.AnyStr, *, encoding: t.Text = 'utf-8'):
    """ Quickly get SHA256 sum for given string.

    >>> sha256sum("hello sha256!")
    '705cb95c164e32feec2aef56f70d73e064afe2e38d40e5189fc5f8cdc84a9eaf'

    Args:
        s (str): String to be encoded.

    Returns:
        Calculated SHA256 sum
    """
    raise NotImplementedError(f'sha256sum not implemented for type: {type(obj)}')


@sha256sum.register(str)
def _(text: str, *, encoding: t.Text = 'utf-8'): # type: ignore
    assert isinstance(text, str)
    return hashlib.sha256(text.encode(encoding)).hexdigest()


@sha256sum.register(bytes)
def _(text: bytes, **_): # type: ignore
    assert isinstance(text, bytes)
    return hashlib.sha256(text).hexdigest()