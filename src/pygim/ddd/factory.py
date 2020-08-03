"""
This contains a generic factory that works in open-closed principle.
"""

from functools import partial
from typing import Mapping, Callable, Iterable, Any
from collections import defaultdict as ddict

from ..system.exceptions import FactoryMethodNotFoundException, FactoryMethodRegisterationException
from ..magic import python_name_mangling

__all__ = ('create_factory', 'FactoryMethodRegisterationException')

_CREATE_PREFIX = "create_"


class Name:
    def __init__(self, name):
        assert isinstance(name, (str, type(None), Name))
        self._name = getattr(name, '_name', None) or name or ""

    def __len__(self):
        return len(self._name)

    def __str__(self):
        return self._name

    def __add__(self, other):
        assert isinstance(other, (str, Name))
        return self.__class__(f"{self._name}.{str(other)}")

    def __iadd__(self, other):
        self._name = self._name + other._name
        return self

    @property
    def with_prefix(self):
        if self._name.startswith(_CREATE_PREFIX):
            return self
        return self.__class__(f'{_CREATE_PREFIX}{self._name}')

    @property
    def without_prefix(self):
        if self._name.startswith(_CREATE_PREFIX):
            return self.__class__(self._name.replace(_CREATE_PREFIX, ''))
        return self

    def as_class_name(self):
        assert self._name.count('.') > 0
        class_name = self._name.split('.')[-1].title().replace('_', '')
        if class_name.endswith("Factory"):
            return class_name
        return f'{class_name}Factory'

    def mangled_attr(self, name):
        return python_name_mangling(self.as_class_name(), name)

    @property
    def namespace(self):
        return ".".join(n for n in self._name.split('.')[:-1] if n)


class Factory(type):
    __factories = ddict(dict)

    def __getattribute__(cls, name):
        try:
            return super().__getattribute__(name)
        except AttributeError:
            map = super().__getattribute__(f"_{cls.__name__}__map")
            try:
                return map[name]
            except KeyError:
                raise FactoryMethodNotFoundException(list(map), name) from None

    def __register(cls, func: Callable, *, name=None, **kwargs):
        """ Register one or more functions.

        >>> Factory.register(lambda: test)
        """
        if not func.__name__.startswith('create'):
            raise FactoryMethodRegisterationException()

        cls.__dict__[python_name_mangling(cls.__name__, "map")][func.__name__] = func
        return func

    def __getitem__(cls, key):
        map = super().__getattribute__(python_name_mangling(cls.__name__, "map"))
        try:
            return map[key]
        except KeyError:
            raise FactoryMethodNotFoundException(list(map), key) from None

    @staticmethod
    def __build_attrs(attrs):
        new_attrs = {}
        for k, v in attrs.items():
            new_attrs[str(Name(k).with_prefix)] = v
            new_attrs[str(Name(k).without_prefix)] = v

        return new_attrs

    def __new__(self, class_name: Name, bases, attrs):
        assert isinstance(class_name, Name)

        new_class_name = class_name.as_class_name()

        try:
            return Factory.__factories[class_name.namespace][new_class_name]
        except KeyError:
            attrs = self.__build_attrs(attrs)

            attrs = {
                class_name.mangled_attr('map'): attrs,
                "register": classmethod(self.__register),
                "__qualname__": f'{class_name.namespace}.{new_class_name}',
                }

            FactoryClass = type.__new__(Factory, new_class_name, (), attrs)
            Factory.__factories[class_name.namespace][new_class_name] = FactoryClass
            return FactoryClass


def create_factory(name=None, mapping: Mapping = None, *, namespace=None):
    name = Name(name)
    namespace = Name(namespace)

    if not name and mapping is None:
        return partial(create_factory, namespace=namespace)

    if not isinstance(mapping, (type(None), Mapping)):
        raise TypeError('do better')

    if mapping is not None and not mapping:
        raise ValueError('do much better')

    return Factory(namespace + name, (), mapping or {})



if __name__ == "__main__":
    import doctest
    doctest.testmod()

