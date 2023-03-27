# -*- coding: utf-8 -*-
"""
This creates a shared class that can be extended
"""

import pygim.typing as t
import pygim.exceptions as ex

__all__ = ['EntangledClass']


AnyClass = t.Type[t.Any]


class EntangledClassError(ex.GimError):
    """ """


class EntangledClassMetaMeta(type):
    __namespaces: t.DefaultDict = {}

    @staticmethod
    def _resolve_namespace(namespace_name: t.Optional[t.Text], class_name: t.Text) -> t.Text:
        if namespace_name is None:
            namespace_name = "pygim"
        return f"{namespace_name}.{class_name}"

    def __call__(self,
                 class_name: t.Text,
                 bases: t.Tuple[AnyClass],
                 namespace: t.Dict[t.Text, t.Any],
                 namespace_name: t.Text = None,
            ) -> t.Any:
        namespace_name = self._resolve_namespace(namespace_name, class_name)
        try:
            existing_class = self.__namespaces[namespace_name]
            for name, obj in namespace.items():
                if name not in existing_class.__dict__:
                    setattr(existing_class, name, obj)
            return existing_class
        except KeyError:
            shared_class = super().__new__(self, class_name, bases, namespace)
            self.__namespaces[namespace_name] = shared_class
            return shared_class


class EntangledClassMeta(type, metaclass=EntangledClassMetaMeta):
    __namespaces = {}

    def __new__(mcls: AnyClass,
                name: t.Text,
                bases: t.Tuple[AnyClass],
                namespace: t.Dict[t.Text, t.Any],
            ) -> t.Any:
        cls = super().__new__(mcls, name, bases, namespace)
        return cls

    def __getitem__(self, key):
        assert self.__bases__ == (object, )
        return EntangledClassMetaMeta.__call__(EntangledClassMetaMeta, self.__name__, (), {}, key)

    def __call__(self, *args: t.Any, **kwds: t.Any) -> t.Any:
        if EntangledClass not in self.__bases__:
            raise EntangledClassError("EntangledClass is abstract class, so please use inheritance!")
        return super().__call__(*args, **kwds)


class classproperty:
    """Read-only @classproperty"""
    def __init__(self, fget):
        self.fget = fget

    def __get__(self, __instance, __class):
        return self.fget(__class)


class EntangledClass(metaclass=EntangledClassMeta):
    @classproperty
    def namespace(cls):
        return "pygim"
