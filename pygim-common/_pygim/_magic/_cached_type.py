# -*- coding: utf-8 -*-
"""
This module implements cached type that can be used to manage singletons.
"""

from ._gimmick import gim_type


__all__ = ["CachedTypeMeta", "CachedType", "create_cached_class"]


class _Templates:
    def __new_nocache__(mcls, name, bases, namespace):
        return super(mcls, mcls).__new__(mcls, name, bases, namespace)

    def __call_nocache__(self, *args, **kwargss):
        return super().__call__(self, *args, **kwargss)

    def __new_cache__(mcls, name, bases, attrs):
        try:
            return mcls.__class_cache[name, bases]
        except Exception:
            cls = super().__new__(mcls, name, bases, attrs)
            mcls.__class_cache[name, bases] = cls
            return cls

    def __call_cache__(self, *args, **kwargs):
        try:
            return self.__instance_cache[self, args]
        except KeyError:
            instance = super().__call__(*args, **kwargs)
            self.__instance_cache[self, args] = instance
            return instance


class CachedTypeMetaMeta(type):
    def __new__(mcls, name, bases, namespace):
        _CachedTypeMeta = super().__new__(mcls, name, bases, namespace)
        _CachedTypeMeta_CI = gim_type("_CachedTypeMeta_CI", bases=(type,))
        _CachedTypeMeta_CI.__new__ = _Templates.__new_cache__
        _CachedTypeMeta_CI.__call__ = _Templates.__call_cache__
        _CachedTypeMeta_C = gim_type("_CachedTypeMeta_C", bases=(type,))
        _CachedTypeMeta_C.__new__ = _Templates.__new_cache__
        _CachedTypeMeta_C.__call__ = _Templates.__call_nocache__
        _CachedTypeMeta_I = gim_type("_CachedTypeMeta_I", bases=(type,))
        _CachedTypeMeta_I.__new__ = _Templates.__new_nocache__
        _CachedTypeMeta_I.__call__ = _Templates.__call_cache__
        _CachedTypeMeta_no = gim_type("_CachedTypeMeta", bases=(type,))
        _CachedTypeMeta_no.__new__ = _Templates.__new_nocache__
        _CachedTypeMeta_no.__call__ = _Templates.__call_nocache__

        assert issubclass(_CachedTypeMeta_CI, type)
        assert issubclass(_CachedTypeMeta_C, type)
        assert issubclass(_CachedTypeMeta_I, type)
        assert issubclass(_CachedTypeMeta_no, type)

        _meta_classes = {
            (True, True): _CachedTypeMeta_CI,
            (True, False): _CachedTypeMeta_C,
            (False, True): _CachedTypeMeta_I,
            (False, False): _CachedTypeMeta_no,
        }
        setattr(_CachedTypeMeta, f"_{name}__meta_classes", _meta_classes)
        return _CachedTypeMeta


class CachedTypeMeta(type, metaclass=CachedTypeMetaMeta):
    """
    A metaclass for creating classes and instances that are cached.

    Caching Classes:
    When a class is created based on the given name, the `CachedType` ensures that it is
    always accessible by its name. This removes the need to import the specific module
    that exposes the class. This caching of classes can have benefits such as minimizing the
    amount of maintenance necessary to import and update these modules within the project.

    Caching Class Instances:
    `CachedType` provides a specialized caching mechanism that caches instances and their
    associated arguments, similar to the LRU Cache. This optimization can have a considerable
    impact on performance, particularly with objects that have finite variations
    of arguments and are immutable.

    Usage:
    To use this metaclass, inherit from it when defining a class.

    Example:
        ```
        class MyClass(metaclass=CachedTypeMeta):
            def __init__(self, arg1, arg2):
                self.arg1 = arg1
                self.arg2 = arg2

        obj1 = MyClass('val1', 'val2')
        obj2 = MyClass('val1', 'val2')  # cached object returned, no new instance created
        obj3 = MyClass('val2', 'val1')  # new instance
        ```
    """

    def __new__(mcls, name, bases=(), attrs=None, *, cache_class=True, cache_instance=True):
        """
        Creates a new class using the specified metaclass and caches the class based on their
        names to ensure they are always accessible by name. If no attributes provided, an empty
        dictionary is used.

        Args:
            mcls:   The `CachedTypeMeta` metaclass.
            name:   The name of the class that is being created.
            bases:  Tuple of parent classes that define the type being created.
            attrs:  The dictionary of defined attributes of the new class.

        Returns:
            A new class using the specified metaclass.
        """
        try:
            return mcls.__meta_classes[cache_class, cache_instance](name, bases, attrs or {})
        except KeyError:
            return super().__new__(mcls, name, bases, attrs)

    def __init__(self, *args, **kwargs):
        """Empty."""

    def __call__(self, *args, **kwargs):
        if self is CachedType:
            raise TypeError(
                f"This class {self.__name__} is abstract and therefore can't be instantinated directly!"
            )

        return self.__instance_cache[self](args, kwargs)

    @classmethod
    def reset_type_cache(mcls, *, type_cache=False, instance_cache=False):
        """
        Clears the cache although this function shouldn't really be called at all, unless,
        it is considered essential to shoot down bridges before crossing them.
        """
        if type_cache:
            mcls.__meta_classes[True, False].reset()
            mcls.__meta_classes[True, True].reset()

        if instance_cache:
            mcls.__meta_classes[False, True].reset()

_USERDEFINED = object()

class CachedType(metaclass=CachedTypeMeta, cache_class=_USERDEFINED, cache_instance=_USERDEFINED):
    """Abstract base class for cached types."""


create_cached_class = CachedTypeMeta
