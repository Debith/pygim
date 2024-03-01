# -*- coding: utf-8 -*-
"""
This module provides utilities for working with attributes.
"""

__all__ = ["safedelattr", "smart_getattr", "mgetattr"]

UNDEFINED = object()

def safedelattr(obj, name):
    try:
        delattr(obj, name)
    except AttributeError:
        pass  # It is already deleted and we are fine with it.


def smart_getattr(obj, name, *, autocall=True, default=UNDEFINED):
    if not hasattr(obj, name):
        if default is UNDEFINED:
            raise AttributeError(f"{obj!r} has no attribute {name!r}")
        return default
    value = getattr(obj, name)
    if autocall and callable(value):
        return value()
    return value


def mgetattr(iterable, name, *, with_obj=True, autocall=True, default=UNDEFINED):
    """ Get attribute from the objects in the iterable. """
    for obj in iterable:
        if hasattr(obj, name):
            if with_obj:
                yield obj, smart_getattr(obj, name, autocall=autocall, default=default)
            else:
                yield smart_getattr(obj, name, autocall=autocall, default=default)

from dataclasses import dataclass
@dataclass
class MultiCall:
    __func_name: str
    __objs: list

    def __call__(self, *args, **kwargs):
        results = []
        for func in mgetattr(self.__objs, self.__func_name, with_obj=False, autocall=False):
            results.append(func(*args, **kwargs))
        return results