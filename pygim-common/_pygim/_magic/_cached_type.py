# -*- coding: utf-8 -*-
"""
This module implements cached type that can be used to manage singletons.
"""

__all__ = ["CachedTypeMeta", "CachedType", "create_cached_class"]


class CachedInstanceMeta(type):
    __instance_cache = {}

    def __new__(mcls, name, bases, namespace):
        return super(CachedInstanceMeta, mcls).__new__(mcls, name, bases, namespace)

    def __call__(self, *args, **kwargs):
        try:
            return self.__instance_cache[self, args]
        except KeyError:
            instance = super().__call__(*args, **kwargs)
            self.__instance_cache[self, args] = instance
            return instance

    @classmethod
    def reset(cls):
        """
        A class method to reset the cached instances of the class.

        Args:
            cls: The class itself.

        Returns:
            None
        """
        cls.__instance_cache = {}


class CachedClassInstanceMeta(CachedInstanceMeta):
    __class_cache = {}

    def __new__(mcls, name, bases, attrs):
        try:
            return mcls.__class_cache[name, bases]
        except Exception:
            cls = super(CachedClassInstanceMeta, mcls).__new__(mcls, name, bases, attrs)
            mcls.__class_cache[name, bases] = cls
            return cls

    @classmethod
    def reset(cls):
        super().reset()
        cls.__class_cache = {}


class CachedClassMeta(type):
    __class_cache = {}

    def __new__(mcls, name, bases, namespace):
        try:
            return mcls.__class_cache[name, bases]
        except Exception:
            cls = super(CachedClassMeta, mcls).__new__(mcls, name, bases, namespace)
            mcls.__class_cache[name, bases] = cls
            return cls

    @classmethod
    def reset(cls):
        cls.__class_cache = {}


class CachedTypeMeta(type):
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

    __meta_classes = {
        (True, True): CachedClassInstanceMeta,
        (True, False): CachedClassMeta,
        (False, True): CachedInstanceMeta,
        (False, False): type,
    }
    _class_cache = dict()  # _Cache(dict)  # filled my metaclass
    __instance_cache = dict()

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
        return mcls.__meta_classes[cache_class, cache_instance](name, bases, attrs or {})

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


class CachedType(metaclass=CachedTypeMeta):
    """Abstract base class for cached types."""


create_cached_class = CachedTypeMeta
