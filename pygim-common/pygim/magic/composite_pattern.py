# -*- coding: utf-8 -*-
"""
This module provides a mechanism to generate classes for Composite pattern.
"""

import abc
from dataclasses import dataclass
import inspect
from typing import Iterable


def composite_pattern(component_name):

    #################################################################

    def _create_component_meta_class(name):
        """
        Create metaclass for the component.
        """
        def __new__(mcls, name, bases, attrs):
            if bases:
                old_class = bases[0]
                if mcls.supports_composite_pattern(old_class):
                    for k, v in attrs.items():
                        setattr(old_class, k, v)
                    mcls.__gimmicks__[old_class.__gimmicks__['composite_pattern']] = old_class
                    return old_class

            instance = abc.ABCMeta.__new__(mcls, name, bases, attrs)
            mcls.__gimmicks__[instance.__gimmicks__['composite_pattern']] = instance
            return instance

        def _is_base_class(cls):
            return cls.__gimmicks__['composite_pattern'] == 'Component'

        def supports_composite_pattern(obj):
            if inspect.isclass(obj):
                try:
                    return obj.__gimmicks__['composite_pattern']
                except (AttributeError, KeyError):
                    return False
            else:
                return supports_composite_pattern(obj.__class__)

        def __call__(self, components):
            _Composite = self.__class__.__gimmicks__['Composite']
            _Leaf = self.__class__.__gimmicks__['Leaf']
            _Component = self.__class__.__gimmicks__['Component']

            def _create_recursively(comps):
                if isinstance(comps, (tuple, list, set)):
                    return abc.ABCMeta.__call__(_Composite, [_create_recursively(c) for c in comps])
                elif self.supports_composite_pattern(comps):
                    return comps
                else:
                    return abc.ABCMeta.__call__(_Leaf, comps)

            instance = _create_recursively(components)
            return instance

        ComponentMeta = type(name, (abc.ABCMeta,), dict(
            __new__=__new__,
            __call__=__call__,
            _is_base_class=staticmethod(_is_base_class),
            supports_composite_pattern=staticmethod(supports_composite_pattern),
            __gimmicks__=dict(),
        ))

        return ComponentMeta

    #################################################################

    def _create_component_class(name, component_meta):
        """
        Create component base class.
        """
        Component = component_meta(name, (abc.ABC,), dict(
            _component=None,
            metaclass=component_meta,
            __annotations__={'_component': object},
            __gimmicks__ = dict(composite_pattern="Component"),
            visit=abc.abstractmethod,
            __iter__=abc.abstractmethod,
        ))
        return dataclass(Component)

    #################################################################

    def _create_leaf_class(name, parent):
        """
        Create component leaf class.
        """
        def __post_init__(self):
            assert not isinstance(self._component, self.__class__)

        def __iter__(self):
            yield self

        def __len__(self):
            return 1

        def __repr__(self):
            return f"{self.__class__.__name__}({repr(self._component)})"

        def visit(self, callable):
            return callable(self._component)

        # __gimmicks__ is used to identify this leaf class and inheriting subclasses
        Leaf = type(name, (parent,), dict(
            __post_init__=__post_init__,
            __iter__=__iter__,
            __gimmicks__=dict(composite_pattern='Leaf'),
            visit=visit,
        ))
        return dataclass(Leaf)

    #################################################################

    def _create_composite_class(name, parent):
        """
        Create component composite class.
        """
        def __post_init__(self):
            assert isinstance(self._component, Iterable)
            assert all(isinstance(c, Component) for c in self._component)

        def __iter__(self):
            for c in self._component:
                yield c

        def __len__(self):
            return len(self._component)

        def visit(self, callable):
            return [c.visit(callable) for c in self._component]

        def add(self, *components):
            for c in components:
                self._component.append(self.__class__.__class__.__gimmicks__['Component'](c))

        def reset(self):
            self._component = []

        # __gimmicks__ is used to identify this composite class and inheriting subclasses
        Composite = type(name, (parent,), dict(
            __post_init__=__post_init__,
            __iter__=__iter__,
            __gimmicks__=dict(composite_pattern='Composite'),
            add=add,
            visit=visit,
            reset=reset,
        ))
        return dataclass(Composite)

    #################################################################

    _component_meta_name = f"{component_name}ComponentMeta"
    _component_name = f"{component_name}Component"
    _composite_name = f"{component_name}Composite"
    _leaf_name = f"{component_name}Leaf"

    ComponentMeta = _create_component_meta_class(_component_meta_name)
    Component = _create_component_class(_component_name, ComponentMeta)
    Leaf = _create_leaf_class(_leaf_name, Component)
    Composite = _create_composite_class(_composite_name, Component)

    return Component, Leaf, Composite