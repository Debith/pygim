# -*- coding: utf-8 -*-
"""
This module contains implementation of Each trait.
"""

import inspect
import pygim.typing as t

__all__ = ['Each', 'for_each']

def is_dunder(name: t.Text):
    return name.startswith('__') and name.endswith('__')


class MultiCall:
    def __init__(self, iterable, func_name):
        self._iterable = iterable
        self._func_name = func_name

    def __call__(self, *args, **kwargs):
        return [getattr(o, self._func_name)(*args, **kwargs) for o in self._iterable]


class EachMeta(type):
    def __call__(self, o=None, **kwargs):
        if o is None:
            return self._handle_dynamic_type(kwargs)

        if inspect.isclass(o):
            return self._handle_class(o, kwargs)

        return self._handle_instance(o, kwargs)

    def _handle_dynamic_type(self, kwargs):
        return DynamicEach(**kwargs)

    def _handle_class(self, o, kwargs):
        return StaticEach(o, **kwargs)

    def _handle_instance(self, iterable, kwargs):
        sub_types = set(type(o) for o in iterable)
        if len(sub_types) == 1:
            sub_type = type(iterable[0])
            attrs = {k: MultiCall(iterable, k) for k in self._sub_type_items(sub_type)}
            return IterableEach(attrs, **kwargs)
        return DynamicEach(**kwargs)

    def _sub_type_items(self, sub_type: type):
        for k, v in sub_type.__dict__.items():
            if is_dunder(k):
                continue
            if not callable(v):
                continue
            yield k

ALL = True

class Each(metaclass=EachMeta):
    """ """


class StaticEach:
    def __init__(self, subtype, *, callable=ALL, property=ALL, dunder=ALL):
        self._sub_type = subtype

    def __get__(self, instance, _):
        for name in dir(self):
            self.__dict__[name] = MultiCall(instance, name)

        return self

    def __dir__(self) -> t.List[t.Text]:
        if not self._sub_type:
            return []
        items = []
        for k, v in self._sub_type.__dict__.items():
            if is_dunder(k):
                continue
            if not callable(v):
                continue
            items.append(k)
        return items

    def __getitem__(self, key):
        assert key in dir(self)
        return MultiCall(self._instance, key)


class IterableEach:
    def __init__(self, attrs, *, callable=ALL, property=ALL, dunder=ALL):
        self.__dict__.update(attrs)

    def __get__(self, instance, _):
        for name in dir(self):
            self.__dict__[name] = MultiCall(instance, name)

        return self

    def __dir__(self) -> t.List[t.Text]:
        return list(self._attrs)

    def __getitem__(self, key):
        return self._attrs[key]


class DynamicEach:

    def __init__(self, *, callable=ALL, property=ALL, dunder=ALL):
        assert any([callable, property, dunder]), "choose at least one"
        self._instance = None

    def __get__(self, instance, _):
        assert isinstance(instance, t.Iterable)
        sub_type = set(type(o) for o in instance)
        assert len(sub_type) <= 1
        self._sub_type = list(sub_type)

        for name in dir(self):
            self.__dict__[name] = MultiCall(instance, name)

        return self

    def __dir__(self) -> t.List[t.Text]:
        if not self._sub_type:
            return []
        items = []
        for k, v in self._sub_type[0].__dict__.items():
            if is_dunder(k):
                continue
            if not callable(v):
                continue
            items.append(k)
        return items

    def __getitem__(self, key):
        assert key in dir(self)
        return MultiCall(self._instance, key)

    def __call__(self, *args: t.Any, **kwds: t.Any) -> t.Any:
        pass


for_each = Each