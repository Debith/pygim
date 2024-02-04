# -*- coding: utf-8 -*-
"""
Python Gimmicks Library.

This module provides various classes and functions for working with Python gimmicks.

Common Library (pip install pygim-common)
----------------------------------------------------------
>>> from pygim.fileio import write_bytes, pickle_and_compress, decompress_and_unpickle
- `write_bytes`: Write bytes to a file.
- `pickle_and_compress`: Pickle and compress an object.
- `decompress_and_unpickle`: Decompress and unpickle an object.

>>> from pygim.gimmicks import gimmick, gim_type, EntangledClass
- `gimmick`: Base class for creating gimmick objects, akin to Python's `object`.
- `gim_type`: Metaclass for creating `gimmick` objects, similar to Python's `type`.
- `EntangledClass`: Enables shared functionality within classes across modules.

>>> from pygim.gimmicks.abc import Interface, AbstractClass
- `Interface`: Define strict interfaces without implementations.
- `AbstractClass`: Create flexible abstract classes, extending `abc.ABC`.

>>> from pygim.performance import dispatch, quick_timer, quick_profile
- `dispatch`: Decorator for creating single and multiple dispatch functions and methods.
- `quick_timer`: Context manager for timing code blocks.
- `quick_profile`: Context manager for profiling code blocks.

>>> from pygim.security import sha256sum, sha256sum_file
- `sha256sum`: Calculate the SHA256 checksum of a string or file.
- `sha256sum_file`: Calculate the SHA256 checksum of a file.

>>> from pygim.testing import diff, measure_coverage, run_tests
- `diff`: Compare two objects together visualizing differences.
- `measure_coverage`: Measure code coverage of given module.
- `run_tests`: Run tests for given module.

>>> from pygim.utils import safedelattr
- `safedelattr`: Deletes attribute from the object and is happy if it is not there.

>>> from pygim.checklib import has_instances, is_subset
- `has_instances`: Check if an iterable contains instances of a specified type.
- `is_subset`: Check if an iterable is a subset of another iterable.

>>> from pygim.explib import GimException, file_error_msg, type_error_msg
- `GimException`: Generic exception that can be used across Python projects.
- `file_error_msg`: Create a file error message.
- `type_error_msg`: Create a type error message.

>>> from pygim.iterlib import flatten, is_container, split
- `flatten`: Flatten nested iterables into a single flat list.
- `is_container`: Check if an object is an iterable container but not a string or bytes.
- `split`: Split an iterable into two based on a specified condition.

Domain Driven Design Library (pip install pygim-ddd)
----------------------------------------------------
>>> from pygim.ddd.interfaces import IEntity, IValueObject, IRootEntity, IRepository
- Interface classes to help with Domain Driven Design.

Primitives Library (pip install pygim-primitives)
-------------------------------------------------
>>> from pygim.primitives import RangeSelector
- `RangeSelector`: Category selector for ranges of values.

>>> from pygim.primitives import PathSet
- `PathSet`: Manages multiple Path objects.

Example Usage
-------------
Example usage of `Gimmick`:
```python
from pygim.gimmicks import Gimmick

class MyGimmick(Gimmick):
    def __init__(self):
        super().__init__()
        # Additional initialization code
"""

# Tiny trick to avoid doctest from evaluating the result of the following import.
__doc__ = "\n".join([f'{line}\n' if line.startswith('>>>')
                     else line for line in __doc__.splitlines()])

from .__version__ import __version__

__author__ = "Teppo Perä"
__email__ = "debith-dev@outlook.com"
