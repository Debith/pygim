# -*- coding: utf-8 -*-
"""
Python Gimmicks Library.

This module provides various classes and functions for working with Python gimmicks.

Classes
-------
gimmick.gimmick
    This class is equivalent to Python's `object`. Inherit from this class to create gimmick objects.
gimmick.gim_type
    This class is equivalent to Python's `type`. It creates `gimmick` objects.
entanged.EntangledClass
    A class that can be shared and extended across modules.
pathset.PathSet
    A class to manage multiple Path objects.

Functions
---------
dispatch.dispatch
    A function that supersedes `singledispatch(method)`.
iterlib.flatten(iterable)
    Convert nested arrays into a single flat array.
iterlib.is_container(obj)
    Check whether an object is iterable but not a string or bytes.
iterlib.split(iterable, condition)
    Split an iterable into two iterables based on a condition function.

Examples
--------
Example usage of `gimmick`:
```python
from pygim.gimmick import gimmick

class MyGimmick(gimmick):
    def __init__(self):
        super().__init__()
        # Additional initialization code
"""

from .__version__ import __version__

__author__ = "Teppo Perä"
__email__ = "debith-dev@outlook.com"
