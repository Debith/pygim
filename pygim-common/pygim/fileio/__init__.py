# -*- coding: utf-8 -*-
"""
File I/O Operations

This module provides functions and utilities for file input/output operations,
including file handling, serialization, and compression.

Functions
---------
write_bytes(file_path, data)
    Write binary data to a file.

pickle_and_compress(obj)
    Serialize and compress an object.

decompress_and_unpickle(data)
    Decompress and unserialize data into an object.

Classes
-------
PathSet
    A class for managing multiple file path objects.
"""

from .fileops import *
from .pathset import *

__all__ = [
    "write_bytes",
    "pickle_and_compress",
    "decompress_and_unpickle",
    "PathSet",
    ]