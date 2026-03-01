# -*- coding: utf-8 -*-
"""
CLI for Python Gimmicks.
"""

from pygim.pathset import PathSet
from pygim.registry import Registry
from pygim.factory import Factory

# Import C++ extension modules explicitly
try:  # normal pybind11 extension import
	from . import repository_v2 as _repo_mod  # type: ignore
	Repository = _repo_mod.Repository  # type: ignore[attr-defined]
	MemoryStrategy = _repo_mod.MemoryStrategy  # type: ignore[attr-defined]
	MssqlStrategy = _repo_mod.MssqlStrategy  # type: ignore[attr-defined]
	Query = _repo_mod.Query  # type: ignore[attr-defined]
	MssqlDialect = _repo_mod.MssqlDialect  # type: ignore[attr-defined]
	__all__ = ["PathSet", "Registry", "Factory", "Repository", "MemoryStrategy",
	           "MssqlStrategy", "Query", "MssqlDialect"]
except Exception:  # pragma: no cover - if compiled extension missing
	__all__ = ["PathSet", "Registry", "Factory"]
