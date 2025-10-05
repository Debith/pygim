# -*- coding: utf-8 -*-
"""Helper utilities for repository extension.

Separated to avoid name collision with compiled extension module `pygim.repository`.
"""
from __future__ import annotations
from typing import Any, Dict

class MemoryStrategy:
    """Simple in-memory strategy for development & tests."""
    def __init__(self):
        self._store: Dict[Any, Any] = {}
    def fetch(self, key):
        return self._store.get(key)
    def save(self, key, value):
        self._store[key] = value
    def __repr__(self):
        return f"MemoryStrategy(size={len(self._store)})"

__all__ = ["MemoryStrategy"]
