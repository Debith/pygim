# -*- coding: utf-8 -*-
"""

"""

try:
    import _pygim.common_fast as _mod
except ImportError:
    from _pygim import _iterlib as _mod


__all__ = ["flatten", "is_container"]

flatten = _mod.flatten
is_container = _mod.is_container
