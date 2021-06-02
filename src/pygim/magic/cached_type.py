"""
This module implements cached type that can be used to manage singletons.
"""

__all__ = ["CachedType"]

from typing import Text, Any

class CachedType(type):
    __class_cache = dict()
    __instance_cache = dict()
    __instance_cache_active = dict()  # Some types are cached and some not.

    def __new__(mcls, name: Text, bases=(), attrs=None, *, cache_class=True, cache_instance=True):
        try:
            if not cache_class:
                raise KeyError("pop!")
            return mcls.__class_cache[name]
        except KeyError:
            cls = mcls.make_class(name, bases, attrs or {})
            mcls.__class_cache[name] = cls
            mcls.__instance_cache_active[cls] = cache_instance
            return cls

    def __init__(self, *args, **kwargs):
        """ Empty. """

    def __call__(self, *args, **kwargs) -> Any:
        try:
            if not self.__instance_cache_active[self]:
                raise KeyError("pop!")
            return self.__instance_cache[self]
        except KeyError:
            instance = super().__call__(*args, **kwargs)
            self.__instance_cache[self] = instance
            return instance

    @classmethod
    def make_class(mcls, name, bases, attrs):
        return super().__new__(mcls, name, bases, attrs)

    @classmethod
    def reset_type_cache(mcls, *, type_cache=False, instance_cache=False):
        """
        Clears the cache although this function shouldn't really be called at all, unless,
        it is considered essential to shoot down bridges before crossing them.
        """
        if type_cache:
            mcls.__class_cache.clear()

        if instance_cache:
            mcls.__instance_cache.clear()