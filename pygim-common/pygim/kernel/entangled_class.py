# -*- coding: utf-8 -*-
"""
This creates a shared class that can be extended
"""

from pygim import exceptions
from collections.abc import Mapping, MutableMapping
from _pygim._magic._cached_type import CachedTypeMeta


__all__ = ["EntangledClass", "overrideable", "overrides"]


def setdefaultattr(obj, name, default):
    """ Sets attribute to object in case it is missing. """
    if isinstance(obj, property):
        obj = obj.fget

    if hasattr(obj, name):
        return getattr(obj, name)

    setattr(obj, name, default)
    return default


def overrideable(func):
    assert callable(func) or isinstance(func, property)
    map = setdefaultattr(func, "__pygim__", {})
    map["overrideable"] = True
    return func


def overrides(func):
    assert callable(func) or isinstance(func, property)
    map = setdefaultattr(func, "__pygim__", {})
    map["overrides"] = True
    return func


_DEFAULT_DICT = dict(overrideable=overrideable, overrides=overrides)
_NAMESPACE_KEY = "__pygim_namespace__"
_DEFAULT_NAMESPACE = "pygim"
_DEFAULT_KEY = ("EntangledClass", ())
_ABSTRACT_ATTR = "__pygim_abstract__"


# TODO: __pygim__ should be assumed to contain data in all situations.
def _getgimdict(obj):
    """ Retrieves __pygim__ dictionary from the object. """
    if isinstance(obj, property):
        obj = obj.fget

    if hasattr(obj, "__pygim__"):
        return getattr(obj, "__pygim__")

    return {}


def _can_override(func_name, new_namespace, old_namespace):
    """ Checks if function can be overridden in `EntangledClass`. """
    assert isinstance(func_name, str), f"Expecting string, got {type(func_name)}"
    assert isinstance(new_namespace, Mapping)
    assert isinstance(old_namespace, Mapping)

    _is_overrideable = _getgimdict(old_namespace[func_name]).get("overrideable", False)
    _can_override = _getgimdict(new_namespace[func_name]).get("overrides", False)

    return _can_override and _is_overrideable


class _NameSpace(metaclass=CachedTypeMeta, cache_class=False, cache_instance=True):
    """ Namespace used to contain its classes.

    For each namespace identified by its name, there is own namespace object.
    """
    def __init__(self, name, *, classes=None):
        self._name = name
        self._classes = classes or {}

    def __setitem__(self, key, value):
        self._classes[key] = value

    def __getitem__(self, key):
        return self._classes[key]


class EntangledClassMetaMeta(type):
    """ This class ensures that all classes are created from scratch and updated with new data. """
    def __new__(mcls, name, bases, namespace):
        namespace.setdefault(_NAMESPACE_KEY, _DEFAULT_NAMESPACE)
        new_meta_class = super().__new__(mcls, name, bases, namespace)
        return new_meta_class

    def _ensure_obj_is_writeable(self, new_namespace, old_newspace):
        """ Safe-guarding against overwriting shared methods. """
        assert isinstance(new_namespace, Mapping)
        assert isinstance(old_newspace, Mapping)

        common = set(new_namespace).intersection(old_newspace)
        allowed = set(["__module__", "overrides", "overrideable", _NAMESPACE_KEY, _ABSTRACT_ATTR])
        overriding = set(f for f in common if _can_override(f, new_namespace, old_newspace))
        unhandled = common - allowed - overriding

        if unhandled:
            raise exceptions.EntangledMethodError(f"Can't override following names: {','.join(unhandled)}")

        return overriding

    def __call__(self, _class_name, _bases, _namespace):
        """Create a new class or find existing from the namespaces."""
        assert isinstance(_class_name, str)
        assert isinstance(_bases, tuple)
        assert isinstance(_namespace, MutableMapping)
        namespace = _NameSpace(_namespace[_NAMESPACE_KEY])

        try:
            existing_class = namespace[_class_name, _bases]
        except KeyError:
            new_class = super().__call__(_class_name, _bases, _namespace)
            namespace[_class_name, _bases] = new_class
            return new_class
        self._extend(existing_class, _namespace)
        return existing_class

    def _extend(self, _existing_class, _namespace):
        """Add new attributes and methods to existing class.

        Args:
            _existing_class (type): This is an existing class found in the namespace.
            _namespace (Mapping): New namespaces that will be added to the class.

        Returns:
            type: returning the class given as an argument.
        """
        assert isinstance(_existing_class, type)
        assert isinstance(_namespace, Mapping)

        overriding = self._ensure_obj_is_writeable(_namespace, _existing_class.__dict__)
        for name, obj in _namespace.items():
            if name not in _existing_class.__dict__ or name in overriding:
                setattr(_existing_class, name, obj)
        return _existing_class


class EntangledClassMeta(type, metaclass=EntangledClassMetaMeta):
    @classmethod
    def __prepare__(cls, _, bases):
        """Prepares namespace for EntangledClass."""
        # Ensure these decorators exists during class definition of subclasses.
        new_map = _DEFAULT_DICT.copy()
        if not bases:
            new_map[_NAMESPACE_KEY] = _DEFAULT_NAMESPACE
            new_map[_ABSTRACT_ATTR] = True
        else:
            namespaces = set(getattr(b, _NAMESPACE_KEY, None) for b in bases if hasattr(b, _NAMESPACE_KEY))
            assert len(namespaces) == 1
            new_map[_NAMESPACE_KEY] = list(namespaces)[0]
            new_map[_ABSTRACT_ATTR] = False

        return new_map

    def __new__(mcls, name, bases, namespace, *args):
        return super().__new__(mcls, name, bases, namespace, *args)

    def __getitem__(self, _key):
        assert not isinstance(_key, bool)
        assert self.__bases__ == (object,)

        key = _key or _DEFAULT_NAMESPACE
        namespaces = _NameSpace(key)
        try:
            EntangledClass = namespaces[_DEFAULT_KEY]
        except KeyError:
            EntangledClass = EntangledClassMetaMeta.__call__(
                EntangledClassMeta,
                "EntangledClass",
                (),
                {_NAMESPACE_KEY: key},
            )

        return EntangledClass

    def __call__(self, *args, **kwds):
        """Create instance of the EntangledClass, ensuring only subclasses can be created."""
        if getattr(self, _ABSTRACT_ATTR):
            raise exceptions.EntangledClassError("EntangledClass is abstract class, so please use inheritance!")
        return super().__call__(*args, **kwds)


class EntangledClass(metaclass=EntangledClassMeta):
    """Helper class to create an entangled class using inheritance."""

    __slots__ = ()
