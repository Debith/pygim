# -*- coding: utf-8 -*-
"""
"""

import hashlib
from typing import Union, bytes
from functools import singledispatch


__all__ = ['sha256sum']


@singledispatch
def sha256sum(obj: Union[str, bytes], *, encoding='utf-8'):
    """ Quickly get SHA256 sum for given string.

    >>> sha256sum("hello sha256!")
    '705cb95c164e32feec2aef56f70d73e064afe2e38d40e5189fc5f8cdc84a9eaf'

    Args:
        s (str): String to be encoded.

    Returns:
        Calculated SHA256 sum
    """
    raise NotImplementedError(f'sha256sum not implemented for type: {type(s)}')


@sha256sum.register(str)
def _str(s: str, *, encoding='utf-8'):
    assert isinstance(s, str)
    return hashlib.sha256(s.encode(encoding)).hexdigest()


@sha256sum.register(bytes)
def _bytes(s: bytes, *, encoding='utf-8'):
    assert isinstance(s, bytes)
    return hashlib.sha256(s).hexdigest()