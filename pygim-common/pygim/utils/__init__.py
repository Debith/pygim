# -*- coding: utf-8 -*-
"""
Utilities

This module provides various utility functions for general-purpose tasks.

Functions
---------
safedelattr(obj, attr_name)
    Safely delete an attribute from an object, ignoring errors if the attribute is missing.

"""

# Your module code goes here

from .attributes import safedelattr

__all__ = [
    "safedelattr",
]
