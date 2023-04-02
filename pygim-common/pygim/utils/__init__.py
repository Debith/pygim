# -*- coding: utf-8 -*-
"""
This package contains various utilities.

NOTE: All the utilities here must be standalone, therefore never should
      import any modules outside from this package.
"""

from .iterable import *
from .performance import *
from .persistence import *
from .security import *
from .misc import *

__all__ = [
    "split",  # Split iterable in two iterables based on condition function.
    "is_container",  # Check whether object is iterable but not string or bytes.
    "flatten",  # Convert nested arrays in to a one flat array.
    "quick_timer",  # Calculate time spent inside code within ´with´-statement.
    "quick_profile",  # Profile code inside `with`-statement.
    "write_bytes",  # Write bytes into a file, can ensure folder structure on write.
    "pickle_and_compress",  # Pickle object, compress it, and optionally write to file.
    "decompress_and_unpickle",  # Decompress obj, unpickle it, while optionally read it from file.
    "sha256sum",  # Provides sha256 for a string.
    "safedelattr",  # Delete attribute, ignoring error when its missing.
]
