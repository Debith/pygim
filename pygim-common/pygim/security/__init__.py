# -*- coding: utf-8 -*-
"""
Security Utilities

This module provides utilities for security-related operations.

Functions
---------
sha256sum(data)
    Calculate the SHA-256 hash value of the given data.

"""

from .shasum import *

__all__ = ["sha256sum"]