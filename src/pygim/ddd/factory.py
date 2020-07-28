"""
This contains a generic factory that works in open-closed principle.
"""

from typing import Mapping, Callable, Iterable, Any

import attr

__all__ = ('create_factory', )


def split(iterable: Iterable[Any], condition: Callable):
    """
    Split a iterable object into two, based on given condition.
    """
    left = []
    right = []

    for it in iterable:
        if condition(it):
            left.append(it)
        else:
            right.append(it)

    return left, right


class FactoryMethodNotFoundException(Exception):
    def __init__(self, available_attributes, name):
        factory_methods, objects = split(available_attributes, lambda aa: aa.startswith('create'))

        if name.startswith('create'):
            msg = f"Unable to find '{name}' among from available factory methods: {', '.join(factory_methods)}"
        else:
            msg = f"Unable to find '{name}' among from available objects: {', '.join(objects)}"

        super().__init__(msg)


def _py_name_mangling(class_name, attr_name):
    return f"_{class_name.lstrip('_')}__{attr_name}"



class Factory(type):
    __factories = dict()

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
        cls.__dict__[_py_name_mangling(cls.__name__, "map")][func.__name__] = func
        return func

    def __new__(self, name, bases, attrs):
        new_class_name = f"{name.title().replace('_', '')}Factory"

        try:
            return Factory.__factories[new_class_name]
        except KeyError:
            attrs = {
                _py_name_mangling(new_class_name, "map"): {},
                "register": classmethod(self.__register),
                }

            FactoryClass = type.__new__(Factory, new_class_name, (), attrs)
            Factory.__factories[new_class_name] = FactoryClass
            return FactoryClass


def create_factory(name, mapping: Mapping = None):
    if not isinstance(mapping, (type(None), Mapping)):
        raise TypeError('do better')

    if mapping is not None and not mapping:
        raise ValueError('do much better')

    return Factory(name, (), mapping or {})



if __name__ == "__main__":
    import doctest
    doctest.testmod()

