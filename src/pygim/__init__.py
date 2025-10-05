# -*- coding: utf-8 -*-
"""
CLI for Python Gimmicks.
"""

from pygim.pathset import PathSet
from pygim.registry import Registry
from pygim.factory import Factory

# Import C++ extension modules explicitly (Repository extension name collides with python package pygim.repository)
try:  # normal pybind11 extension import
	from . import repository as _repo_mod  # type: ignore
	Repository = _repo_mod.Repository  # type: ignore[attr-defined]
	__all__ = ["PathSet", "Registry", "Factory", "Repository"]
except Exception:  # pragma: no cover - if compiled extension missing
	__all__ = ["PathSet", "Registry", "Factory"]
