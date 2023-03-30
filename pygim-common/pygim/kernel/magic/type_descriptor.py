# -*- coding: utf-8 -*-
"""

"""

from dataclasses import dataclass, field

from typing import Any

__all__ = ["TypeDescriptor"]

class TypeDescriptor:
    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return getattr(obj, '_name', None)

    def __set__(self, obj, value):
        setattr(obj, '_name', value.title())

    def __delete__(self, obj):
        raise AttributeError("Cannot delete attribute")

    def __set_name__(self, owner, name):
        self._parent_class = owner

    def __call__(self, *args, **kwargs):
        return self
