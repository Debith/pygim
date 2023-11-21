# -*- coding: utf-8 -*-
"""
Python Gimmicks Library

This module provides various gimmicks and utilities for Python programming.

Classes
-------
EntangledClass
    A class that can be shared and extended across modules. It allows for easy collaboration and reuse of functionality.

gim_type
    A utility class equivalent to Python's `type`. It creates `gimmick` objects with added gimmick-specific features.

gimmick
    A base class that serves as a foundation for gimmick objects. Inherit from this class to create custom gimmick subclasses.

Notes
-----
- Comprehensive examples for `EntangledClass` can be found from `python-gimmicks/docs/examples/entangled_classes`.
- Comprehensive examples for `gim_type` and `gimmick`can be found from `python-gimmicks/docs/examples/traits`.
"""

from _pygim._magic._gimmick import gim_type, gimmick
from .entangled import *

__all__ = [
    "gim_type",
    "gimmick",
    "EntangledClass",
]