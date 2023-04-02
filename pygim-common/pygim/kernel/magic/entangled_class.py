# -*- coding: utf-8 -*-
"""
This creates a shared class that can be extended
"""

import pygim.exceptions as ex
from .cached_type import CachedTypeMeta


__all__ = ["EntangledClass", "EntangledMethodError", "overrideable", "overrides"]


class EntangledError(ex.GimError):
    """Base class for entanglement errors."""


class EntangledClassError(EntangledError):
    """Raised when issue detected with entangled class."""


class EntangledMethodError(EntangledError):
    """Raised when issue detected with methods of entangled class."""


def setdefaultattr(obj, name, default):
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


def getgimdict(obj):
    if isinstance(obj, property):
        obj = obj.fget

    if hasattr(obj, "__pygim__"):
        return getattr(obj, "__pygim__")

    return {}


def can_override(func, new_namespace, old_namespace):
    _is_overrideable = getgimdict(old_namespace[func]).get("overrideable", False)
    _can_override = getgimdict(new_namespace[func]).get("overrides", False)
    return _can_override and _is_overrideable


from dataclasses import dataclass, field


@dataclass(frozen=True)
class _NameSpace(metaclass=CachedTypeMeta, cache_class=False, cache_instance=True):
    _name: str
    _classes: dict = field(default_factory=dict)

    def __setitem__(self, key, value):
        self._classes[key] = value

    def __getitem__(self, key):
        return self._classes[key]


class EntangledClassMetaMeta(type):
    def __new__(mcls, name, bases, namespace):
        namespace.setdefault(_NAMESPACE_KEY, _DEFAULT_NAMESPACE)
        new_meta_class = super().__new__(mcls, name, bases, namespace)
        return new_meta_class

    def _ensure_obj_is_writeable(self, new_namespace, old_newspace):
        """ """
        common = set(new_namespace).intersection(old_newspace)
        allowed = set(["__module__", "overrides", "overrideable", _NAMESPACE_KEY, _ABSTRACT_ATTR])
        overriding = set(f for f in common if can_override(f, new_namespace, old_newspace))
        unhandled = common - allowed - overriding

        if unhandled:
            raise EntangledMethodError(f"Can't override following names: {','.join(unhandled)}")

        return overriding

    def __call__(self, _class_name, _bases, _namespace):
        """Create a new class or find existing from the namespaces."""
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
        overriding = self._ensure_obj_is_writeable(_namespace, _existing_class.__dict__)
        for name, obj in _namespace.items():
            if name not in _existing_class.__dict__ or name in overriding:
                setattr(_existing_class, name, obj)
        return _existing_class


class EntangledClassMeta(type, metaclass=EntangledClassMetaMeta):
    @classmethod
    def __prepare__(cls, name, bases):
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
            raise EntangledClassError("EntangledClass is abstract class, so please use inheritance!")
        return super().__call__(*args, **kwds)


class EntangledClass(metaclass=EntangledClassMeta):
    """Helper class to create an entangled class using inheritance."""

    __slots__ = ()
