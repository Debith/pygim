# -*- coding: utf-8 -*-
"""
This module implements cached type that can be used to manage singletons.
"""

__all__ = ["CachedTypeMeta", "CachedType", "create_cached_class"]

import copy

class ClassBuilder:
    def __init__(self, name, bases, namespace, factory=None):
        self._mcls = type
        self.name = name
        self._bases = bases
        self._namespace = namespace
        self._factory = factory or type.__new__

    def updated(self, **kwargs):
        new = copy.deepcopy(self)
        assert id(new) != id(self)
        new._namespace.update(kwargs)
        return new

    def create(self, **kwargs):
        return self._factory(self._mcls, self.name, self._bases, self._namespace)


new_caching_class = ClassBuilder("_CachingMeta", (type, ), {})

class _func_wrap:
    def __init__(self, func):
        self._func = func

    def __get__(self, __instance, __class):
        if __instance:
            return self._func.__get__(__instance, __class)
        return self._func.__get__(None, __class)


def make_meta(_cache_class, _cache_instance):
    def __new_nocache__(mcls, name, bases, namespace):
        return super(mcls.__class__, mcls).__new__(mcls, name, bases, namespace)

    def __call_nocache__(self, *args, **kwargs):
        return super(self.__class__, self).__call__(*args, **kwargs)

    new_class = new_caching_class.updated(
        __new__=__new_nocache__,
        __call__=__call_nocache__,
        )

    def __new_cache__(mcls, name, bases, attrs):
        try:
            return mcls._class_cache[name, bases]
        except KeyError:
            cls = super(mcls, mcls).__new__(mcls, name, bases, attrs)
            mcls._class_cache[name, bases] = cls
            return cls

    def __call_cache__(self, *args, **kwargs):
        try:
            return self._instance_cache[self, args]
        except KeyError:
            instance = super(self.__class__, self).__call__(*args, **kwargs)
            self._instance_cache[self, args] = instance
            return instance

    if _cache_class:
        new_class = new_class.updated(__new__=_func_wrap(__new_cache__), _class_cache={})
        new_class.name += "Class"

    if _cache_instance:
        new_class = new_class.updated(__call__=_func_wrap(__call_cache__), _instance_cache={})
        new_class.name += "Instance"

    cls = new_class.create()


    return cls
import inspect

class CachedTypeMetaMeta(type):
    def __new__(mcls, name, bases, namespaces):
        mclses = {(_cls, _inst): make_meta(_cls, _inst) for _cls, _inst in (
            (True, True), (True, False), (False, True), (False, False))}
        namespaces["_meta_classes"] = mclses
        cls = super().__new__(mcls, name, bases, namespaces)
        cls._meta_classes[_USER_DEFINED, _USER_DEFINED] = super(mcls, cls).__new__
        return cls

    def __call__(self, name, bases=(), namespaces=None, **kwargs):
        namespaces = namespaces or {}
        if not kwargs:
            return super(self, self).__new__(self, name, bases, namespaces)

        return super(self, self).__call__(self, name, bases, namespaces, **kwargs)
        cls._meta_classes = _CachingClasses
        return cls

_USER_DEFINED = type("USER_DEFINED", (), {})()


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
    def __new__(mcls, name, bases=(), attrs=None, *, cache_class=_USER_DEFINED, cache_instance=_USER_DEFINED):
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
        cached_mcls = mcls._meta_classes[cache_class, cache_instance]
        return cached_mcls(name, bases, attrs or {})

    def __init__(self, *args, **kwargs):
        """Empty."""

    def __call__(self, *args, **kwargs):
        if self is CachedType:
            raise TypeError(
                f"This class {self.__name__} is abstract and therefore can't be instantinated directly!"
            )

        return self._instance_cache[self](args, kwargs)

    @classmethod
    def reset_type_cache(mcls, *, type_cache=False, instance_cache=False):
        """
        Clears the cache although this function shouldn't really be called at all, unless,
        it is considered essential to shoot down bridges before crossing them.
        """
        if type_cache:
            mcls._meta_classes[True, False].reset()
            mcls._meta_classes[True, True].reset()

        if instance_cache:
            mcls._meta_classes[False, True].reset()


class CachedType(metaclass=CachedTypeMeta):
    """Abstract base class for cached types."""



create_cached_class = CachedTypeMeta
