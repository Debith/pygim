# -*- coding: utf-8 -*-
"""
Utilities

This module provides various utility functions for general-purpose tasks.

Functions
---------
safedelattr(obj, attr_name)
    Safely delete an attribute from an object, ignoring errors if the attribute is missing.
smart_getattr(obj, attr_name, *, autocall=True, default=UNDEFINED)
    Get an attribute from an object and optionally call it.
"""

# Your module code goes here

from .attributes import *

__all__ = [
    "safedelattr",
    "smart_getattr"
]
