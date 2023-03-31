# -*- coding: utf-8 -*-
"""
This creates a shared class that can be extended
"""

import pygim.exceptions as ex
from .support import classproperty

__all__ = ['EntangledClass', "EntangledMethodError"]


class EntangledError(ex.GimError):
    """ """

class EntangledClassError(EntangledError):
    """ """


class EntangledMethodError(EntangledError):
    """ """


def setdefaultattr(obj, name, default):
    if isinstance(obj, property):
        obj = obj.fget

    if hasattr(obj, name):
        return getattr(obj, name)

    setattr(obj, name, default)
    return default


def overrideable(func):
    assert callable(func) or isinstance(func, property)
    map = setdefaultattr(func, '__pygim__', {})
    map['overrideable'] = True
    return func


def overrides(func):
    assert callable(func) or isinstance(func, property)
    map = setdefaultattr(func, '__pygim__', {})
    map['overrides'] = True
    return func


def getgimdict(obj):
    if isinstance(obj, property):
        obj = obj.fget

    if hasattr(obj, '__pygim__'):
        return getattr(obj, '__pygim__')

    return {}


def can_override(func, new_namespace, old_namespace):
    _is_overrideable = getgimdict(old_namespace[func]).get('overrideable', False)
    _can_override = getgimdict(new_namespace[func]).get('overrides', False)
    return _can_override and _is_overrideable


class EntangledClassMetaMeta(type):
    __namespaces = {}

    @staticmethod
    def _resolve_namespace(namespace_name, class_name):
        if namespace_name is None:
            namespace_name = "pygim"
        return f"{namespace_name}.{class_name}"

    def _ensure_obj_is_writeable(self, new_namespace, old_newspace):
        """ """
        common = set(new_namespace).intersection(old_newspace)
        allowed = set(['__module__', 'overrides', 'overrideable'])
        overriding = set(f for f in common if can_override(f, new_namespace, old_newspace))
        unhandled = common - allowed - overriding

        if unhandled:
            raise EntangledMethodError(f"Can't override following names: {','.join(unhandled)}")

        return overriding

    def __call__(self, _class_name, _bases, _namespace, _namespace_name=None):
        """ Create a new class or find existing from the namespaces. """
        namespace_name = self._resolve_namespace(_namespace_name, _class_name)

        try:
            existing_class = self.__namespaces[namespace_name]
            overriding = self._ensure_obj_is_writeable(_namespace, existing_class.__dict__)
            for name, obj in _namespace.items():
                if name not in existing_class.__dict__ or name in overriding:
                    setattr(existing_class, name, obj)
            return existing_class
        except KeyError:
            class_name = namespace_name if _namespace_name else _class_name
            shared_class = super().__new__(self, class_name, _bases, _namespace)
            self.__namespaces[namespace_name] = shared_class
            return shared_class


class EntangledClassMeta(type, metaclass=EntangledClassMetaMeta):
    @classmethod
    def __prepare__(cls, name, bases):
        return dict(overrideable=overrideable, overrides=overrides)

    def __new__(mcls, name, bases, namespace):
        cls = super().__new__(mcls, name, bases, namespace)
        return cls

    def __getitem__(self, key):
        assert self.__bases__ == (object, )
        return EntangledClassMetaMeta.__call__(EntangledClassMeta, self.__name__, (EntangledClass,), {}, key)

    def __call__(self, *args, **kwds):
        if not issubclass(self, EntangledClass) or self is EntangledClass:
            raise EntangledClassError("EntangledClass is abstract class, so please use inheritance!")
        return super().__call__(*args, **kwds)


class EntangledClass(metaclass=EntangledClassMeta):
    @classproperty
    def namespace(cls):
        return "pygim"
