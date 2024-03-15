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
to_snake_case(str)
    Convert a string to ``snake_case``.
to_human_case(str)
    Convert a string to ``Human Readable Format``.
to_kebab_case(str)
    Convert a string to ``kebab-case``.
to_pascal_case(str)
    Convert a string to ``PascalCase``.
to_camel_case(str)
    Convert a string to ``camelCase``.
"""

# Your module code goes here

from .attributes import *
from .str_converters import *

__all__ = [
    "safedelattr",
    "smart_getattr",
    "to_snake_case",
    "to_human_case",
    "to_kebab_case",
    "to_pascal_case",
    "to_camel_case",
]
