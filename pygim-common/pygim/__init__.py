# -*- coding: utf-8 -*-
"""
Python Gimmicks Library.
"""

import importlib.abc
import importlib.util
import os
import sys

from .__version__ import __version__

__author__ = "Teppo Perä"
__email__ = "debith-dev@outlook.com"

class CustomFinder(importlib.abc.MetaPathFinder):
    def __init__(self, base_path):
        self.base_path = base_path

    def find_spec(self, fullname, path, target=None):
        if "pygim" not in fullname:
            return None
        if path is None or path == self.base_path:
            module_name = fullname.split('.')[-1]
            file_path = os.path.join(self.base_path, f"{module_name}.py")
            if os.path.isfile(file_path):
                spec = importlib.util.spec_from_file_location(fullname, file_path)
                return spec

        return None

    def exec_module(self, *args):
        return super().exec_module(*args)

# Example usage
custom_base_path = "/path/to/custom/directory"
sys.meta_path.insert(0, CustomFinder(custom_base_path))

from .kernel import *

__all__ = [
    "EntangledClass",  # A class that can be shared and extended across modules.
    "PathSet",  # A class to manage multiple Path objects.
]
