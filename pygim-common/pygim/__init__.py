# -*- coding: utf-8 -*-
"""
Python Gimmicks Library.
"""

from .__version__ import __version__

__author__ = "Teppo Perä"
__email__ = "debith-dev@outlook.com"

from .kernel import *
from _pygim._magic._gimmick import gimmick, gim_type
from _pygim._magic._dispatcher import dispatch

__all__ = [
    "gimmick",      # This is equivalent to Python's `object`. Inherit this!
    "gim_type",     # This is equivalent to Python's `type`, creates `gimmick`s.
    "dispatch",     # This is supercedes singledispatch(method).

    "EntangledClass",  # A class that can be shared and extended across modules.
    "PathSet",  # A class to manage multiple Path objects.
]
