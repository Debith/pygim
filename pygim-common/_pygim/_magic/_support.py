# -*- coding: utf-8 -*-
"""
Magical support functions and classes.
"""

__all__ = ["classproperty"]


class classproperty:
    """Read-only @classproperty"""

    def __init__(self, fget):
        self.fget = fget

    def __get__(self, _, __class):
        return self.fget(__class)
