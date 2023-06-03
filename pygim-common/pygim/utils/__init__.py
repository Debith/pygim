# -*- coding: utf-8 -*-
"""
This package contains various utilities.

NOTE: All the utilities here must be standalone, therefore never should
      import any modules outside from this package.
"""

from .misc import safedelattr

__all__ = [
    "decompress_and_unpickle",  # Decompress obj, unpickle it, while optionally read it from file.
    "diff",                     # Compares two dictionaries and visually displays their differences.
    "flatten",                  # Convert nested arrays in to a one flat array.
    "format_dict",              # Function that formats dict in pretty way.
    "has_instances",            # As `isinstance` but for objects inside iterable.
    "pickle_and_compress",      # Pickle object, compress it, and optionally write to file.
    "quick_profile",            # Profile code inside `with`-statement.
    "quick_timer",              # Calculate time spent inside code within ´with´-statement.
    "safedelattr",              # Delete attribute, ignoring error when its missing.
    "sha256sum",                # Provides sha256 for a any arguments.
    "write_bytes",              # Write bytes into a file, can ensure folder structure on write.
]
