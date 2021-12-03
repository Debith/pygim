""" This module contains main gimmick object with selected set of features. """

__all__ = ["GimObjectMeta", "GimObject"]

from typing import Text, Any


class GimObjectMeta(type):
    def __new__(mcls, name: Text, bases=(), attrs=None, *, instance_cached=False):
        cls = mcls.make_class(name, bases, attrs or {})
        mcls.__class_cache[name] = cls
        mcls.__instance_cache_active[cls] = cache_instance
        return cls


class GimObject(metaclass=GimObjectMeta):
    pass
