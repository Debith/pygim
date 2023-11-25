# -*- coding: utf-8 -*-
"""
This creates a shared class that can be extended
"""

from _pygim._magic._entangled import EntangledClassMeta, overrideable, overrides


__all__ = ["EntangledClass", "overrideable", "overrides"]


class EntangledClass(metaclass=EntangledClassMeta):
    """Helper class to create an entangled class using inheritance."""

    __slots__ = ()
