# -*- coding: utf-8 -*-
"""

"""

# Descriptor that can cache the result of a method call

from _pygim._support import UNDEFINED

from typing import Any
import weakref
import gc


class GimPropertyMeta(type):
    _cache = {}
    def __new__(mcls, name, bases, namespace, **_):
        return super().__new__(mcls, name, bases, namespace)

    def __init__(cls, *args, **_):
        super().__init__(*args)

    def __call__(self, *args: Any, cached=False, **kwargs: Any) -> Any:
        instance = super().__call__(self._cache, *args, **kwargs)
        #gc.callbacks.append(instance._gc_callback)
        return instance


class gim_property(metaclass=GimPropertyMeta):
    """
    A decorator that converts a method into a cached property.

    Parameters
    ----------
    func : function
        The function to be converted into a cached property.

    Methods
    -------
    __get__
        Gets the value of the cached property.

    Notes
    -----
    The `cached_property` decorator converts a method into a cached property. This means that
    the result of the method is cached and returned on subsequent calls, rather than being
    recalculated each time the method is called. This can be useful when the method is expensive
    to calculate, or when you want to ensure that the result is consistent across multiple calls.
    """

    def __init__(self, cache):
        self.__func = None
        self.__name = None
        self._cache = cache

    def __set_name__(self, __class, __name):
        assert self.__name is None, f"__name already set to {self.__name!r}"
        self.__name = __name

    def __get__(self, instance, owner):
        assert self.__name is not None, f"__name not set"
        if instance is None:
            return self
        self._instance = instance

        try:
            cache = self._cache[instance]
        except KeyError:
            self._cache[instance] = cache = {}
        except AttributeError:
            return self.__func(instance)

        value = cache.get(self.__name, UNDEFINED)
        if value is UNDEFINED:
            cache[self.__name] = value = self.__func(instance)
        return value

    def __call__(self, func):
        assert self.__func is None, f"__func already set to {self.__func!r}"
        self.__func = func
        return self

    def _gc_callback(self, *args, **kwargs):
        return
