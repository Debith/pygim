# -*- coding: utf-8 -*-
"""
Functions
---------

flatten(iterable)
    Convert nested arrays into a single flat array.
is_container(obj)
    Check whether an object is iterable but not a string or bytes.
split(iterable, condition)
    Split an iterable into two iterables based on a condition function.
"""

try:
    import _pygim.common_fast as _lib
except ImportError:
    from _pygim import _iterlib as _lib
from _pygim._iterlib import split, flatten, chunks, dictify


__all__ = ["flatten", "is_container", "split", "chunks", "dictify", "tuplify"]

tuplify = _lib.tuplify
is_container = _lib.is_container
