# -*- coding: utf-8 -*-
"""
This module provides building blocks for creating applications and libraries.

Common Library (pip install pygim-common)
-----------------------------------------

- `ID`: A fast unique identifier generator.
>>> from pygim.primitives import ID
>>> id = ID(123)
>>> id
<ID:123>
"""

from _pygim.common_fast import ID
__all__ = ["ID"]

try:
    from pygim._primitives_ext import *
    from pygim._primitives_ext import __all__ as _ext_all
    __all__ += _ext_all
except ImportError:
    pass
