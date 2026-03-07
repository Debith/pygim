# -*- coding: utf-8 -*-
"""
CLI for Python Gimmicks.
"""

from pygim.pathset import PathSet
from pygim.registry import Registry
from pygim.factory import Factory

# Import C++ extension modules explicitly
try:  # normal pybind11 extension import
	from . import _repository as _repo_mod  # type: ignore
	Repository = _repo_mod.Repository  # type: ignore[attr-defined]
	Query = _repo_mod.Query  # type: ignore[attr-defined]
	acquire_repository = _repo_mod.acquire_repository  # type: ignore[attr-defined]
	StatusPrinter = _repo_mod.StatusPrinter  # type: ignore[attr-defined]
	__all__ = ["PathSet", "Registry", "Factory", "Repository",
	           "Query", "acquire_repository", "StatusPrinter"]
except Exception:  # pragma: no cover - if compiled extension missing
	__all__ = ["PathSet", "Registry", "Factory"]
