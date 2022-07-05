# -*- coding: utf-8 -*-
"""
This module provides a mechanism to generate classes for Composite pattern.
"""

import abc
from dataclasses import dataclass
from typing import Iterable
from pygim import typing as t

def generate_composite_pattern_classes(
    component_name: t.Text,
    operation_name: t.Text = "execute",
    ):
    """Generates three classes that can be used as composite pattern.

    Args:
        component_name (str): Name used for the pattern.

    Raises:
        TypeError: When attempting to do deep inheritance.

    Returns:
        Three classes new classes matching `component_name`. The
        new classes are Component, Leaf, and Composite in that
        order.
    """

    #################################################################

    def _create_component_meta_class(name):
        """
        Create metaclass for the component.
        """
        def __new__(mcls, name, bases, attrs):
            if mcls.supports_composite_pattern(attrs):
                new_class = abc.ABCMeta.__new__(mcls, name, bases, attrs)
                mcls.__gimmicks__[new_class.__gimmicks__['composite_pattern']] = new_class
                return new_class

            # This updates the existing class.
            old_class = bases[0]
            _final_name = f"Final{old_class.__gimmicks__['composite_pattern']}"
            if _final_name in mcls.__gimmicks__:
                raise TypeError(f'Refusing to extend class more than once!')
            mcls.__gimmicks__[_final_name] = True
            for k, v in attrs.items():
                setattr(old_class, k, v)
            return old_class

        def _is_base_class(cls):
            return cls.__gimmicks__['composite_pattern'] == 'Component'

        def supports_composite_pattern(attrs: t.Mapping):
            try:
                return attrs["__gimmicks__"]['composite_pattern']
            except (KeyError, TypeError):
                return False

        def __call__(self, components):
            _Composite = self.__class__.__gimmicks__['Composite']
            _Leaf = self.__class__.__gimmicks__['Leaf']

            def _create_recursively(comps):
                if isinstance(comps, (tuple, list, set)):
                    return abc.ABCMeta.__call__(_Composite, [_create_recursively(c) for c in comps])
                elif self.supports_composite_pattern(comps.__class__.__dict__):
                    return comps
                else:
                    return abc.ABCMeta.__call__(_Leaf, comps)

            instance = _create_recursively(components)
            return instance

        def __setattr__(self, name, obj):
            try:
                LeafClass = self.__class__.__gimmicks__['Leaf']
                ComponentClass = self.__class__.__gimmicks__['Component']
                if getattr(ComponentClass, name) is abc.abstractmethod:
                    abc.ABCMeta.__setattr__(LeafClass, name, obj.__get__(None, LeafClass))
            except (AttributeError, KeyError):
                abc.ABCMeta.__setattr__(self, name, obj)

        ComponentMeta = type(name, (abc.ABCMeta,), dict(
            __new__=__new__,
            __call__=__call__,
            __setattr__=__setattr__,
            _is_base_class=staticmethod(_is_base_class),
            supports_composite_pattern=staticmethod(supports_composite_pattern),
            __gimmicks__=dict(),
        ))

        return ComponentMeta

    #################################################################

    def _create_component_class(name, op_name, *, meta_class):
        """
        Create component base class.
        """
        attrs = dict(
            __component=None,
            metaclass=meta_class,
            __annotations__={'__component': object},
            __gimmicks__ = dict(composite_pattern="Component"),
            __iter__=abc.abstractmethod,
            visit=abc.abstractmethod,
        )
        attrs[op_name] = abc.abstractmethod
        Component = meta_class(name, (abc.ABC,), attrs)
        return dataclass(Component)

    #################################################################

    def _create_leaf_class(name, op_name, *, base_class):
        """
        Create component leaf class.
        """
        def __post_init__(self):
            assert not isinstance(self.__component, self.__class__)

        def __iter__(self):
            yield self

        def __len__(self):
            return 1

        def __repr__(self):
            return f"{self.__class__.__name__}({repr(self.__component)})"

        def visit(self, callable):
            return callable(self.__component)

        def op(self, *args, **kwargs):
            return self.__component

        # __gimmicks__ is used to identify this leaf class and inheriting subclasses
        attrs = dict(
            __post_init__=__post_init__,
            __iter__=__iter__,
            __repr__=__repr__,
            __gimmicks__=dict(composite_pattern='Leaf'),
            visit=visit,
        )
        attrs[op_name] = op
        Leaf = type(name, (base_class,), attrs)
        return dataclass(Leaf)

    #################################################################

    def _create_composite_class(name, op_name, *, base_class):
        """
        Create component composite class.
        """
        def __post_init__(self):
            assert isinstance(self.__component, Iterable)
            assert all(isinstance(c, Component) for c in self.__component)

        def __iter__(self):
            for c in self.__component:
                yield c

        def __repr__(self):
            return f"{self.__class__.__name__}({repr(self.__component)})"

        def __len__(self):
            return len(self.__component)

        def __getitem__(self, index: int):
            return self.__component[index]

        def visit(self, callable):
            return [c.visit(callable) for c in self.__component]

        def append(self, component: Component):
            self.__component.append(self.__class__.__class__.__gimmicks__['Component'](component))

        def insert(self, index: int, component: Component):
            self.__component.insert(index, self.__class__.__class__.__gimmicks__['Component'](component))

        def reset(self):
            self.__component = []

        def op(self, *args, **kwargs):
            return [getattr(c, op_name)(*args, **kwargs) for c in self.__component]

        # __gimmicks__ is used to identify this composite class and inheriting subclasses
        attrs = dict(
            __post_init__=__post_init__,
            __iter__=__iter__,
            __repr__=__repr__,
            __getitem__=__getitem__,
            __gimmicks__=dict(composite_pattern='Composite'),
            append=append,
            insert=insert,
            visit=visit,
            reset=reset,
        )
        attrs[op_name] = op
        Composite = type(name, (base_class,), attrs)
        return dataclass(Composite)

    #################################################################

    _component_meta_name = f"{component_name}ComponentMeta"
    _component_name = f"{component_name}Component"
    _composite_name = f"{component_name}Composite"
    _leaf_name = f"{component_name}Leaf"

    ComponentMeta = _create_component_meta_class(_component_meta_name)
    Component = _create_component_class(_component_name, operation_name, meta_class=ComponentMeta)
    Leaf = _create_leaf_class(_leaf_name, operation_name, base_class=Component)
    Composite = _create_composite_class(_composite_name, operation_name, base_class=Component)

    return Component, Leaf, Composite