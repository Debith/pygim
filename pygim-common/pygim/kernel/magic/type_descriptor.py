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
        print(f"Name descriptor set on attribute '{name}' in class '{owner.__name__}'")

    def __call__(self, *args, **kwargs):
        return field(default_factory=self._parent_class)
