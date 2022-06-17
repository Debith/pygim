    # -*- coding: utf-8 -*-
"""

"""

import dis
import abc
from ast import Call
import typing as t
from dataclasses import dataclass
from pygim.utils import iterable
import enum
import inspect
from collections import namedtuple
import types

__all__ = ['Callable']


class ObjectFactoryMeta(type):
    def _is_base_class(self):
        pass

    def _is_sub_class(self):
        pass

    def _handle_base_class(self, *args, **kwargs):
        pass

    def _handle_sub_class(self, *args, **kwargs):
        pass

    def __call__(self, *args, **kwargs):
        pass



class Mode(enum.IntEnum):
    INDEPENDENT = enum.auto()
    PIPELINE = enum.auto()


def composite_pattern(component_name, abstract_methods=None):
    _component_meta_name = f"{component_name}ComponentMeta"
    _component_name = f"{component_name}Component"
    _composite_name = f"{component_name}Composite"
    _leaf_name = f"{component_name}Leaf"

    class _Meta:
        def __new__(mcls, name, bases, attrs):
            instance = super(ComponentMeta, mcls).__new__(mcls, name, bases, attrs)
            return instance

        @staticmethod
        def _is_sub_class(cls):
            return cls.__bases__ != (object, )

        def __call__(self, input_data, *args, **kwargs):
            if self._is_sub_class(self):
                return super(ComponentMeta, self).__call__(input_data, *args, **kwargs)

            if isinstance(input_data, (tuple, set, list)):
                return Composite(input_data, *args, **kwargs)
            else:
                return Leaf(input_data, *args, **kwargs)

    ComponentMeta = type(_component_meta_name, (type,), dict(_Meta.__dict__))

    class _Component(metaclass=ComponentMeta):
        def __init__(self, component):
            assert all(isinstance(c, Component) for c in components)
            self._component = component

    class _LeafBase:
        def __iter__(self):
            yield self._component

    class _CompositeBase:
        def __init__(self, components):
            self._components = components

        def __iter__(self):
            for c in self._components:
                yield from c

    Component = ComponentMeta(_component_name, (), dict(metaclass=ComponentMeta))
    Leaf = ComponentMeta(_leaf_name, (Component, ), dict(_LeafBase.__dict__))
    Composite = ComponentMeta(_composite_name, (Component, ), dict(_CompositeBase.__dict__))

    ComponentMeta.__call__.__globals__["Leaf"] = Leaf
    ComponentMeta.__call__.__globals__["Composite"] = Composite
    ComponentMeta.__call__.__globals__["Component"] = Component

    return Component, Leaf, Composite


Callable, CallableLeaf, CallableComposite = composite_pattern("Callable", abstract_methods=['__call__'])

class MultiCallable(CallableComposite):  # type: ignore
    def __call__(self, *args, **kwargs):
        return [f(*args, **kwargs) for f in self]


class SingleCallable(CallableLeaf):  # type: ignore
    def __call__(self, *args, **kwargs):
        return next(self.__iter__())(*args, **kwargs)



'''
class CallableMeta(abc.ABCMeta, metaclass=CallableMetaMeta):
    """ Factory meta-class for Callables. """

    def __new__(mcls, name, bases, attrs, **kwargs):
        class GimCallableBase:
            __gim_callable_mode__ = kwargs.get('mode')

        if not bases:
            attrs.update(GimCallableBase.__dict__)

        new_class = super().__new__(mcls, name, bases, attrs)

        if new_class.__call__ is abc.abstractmethod:
            subclass_name = new_class.__gim_callable_mode__.name.title() + name

            if new_class.__gim_callable_mode__ == Mode.PIPELINE:
                class Base:
                    def __init__(self, callables):
                        self._callables = callables

                    def __call__(self, *args, **kwargs):
                        func = next(self)
                        result = func(*args, **kwargs)
                        for func in self:
                            result = func(result)
                        return result

                    def __iter__(self):
                        for c in self._callables:
                            yield from c

            elif new_class.__gim_callable_mode__ == Mode.INDEPENDENT:
                class Base:
                    def __init__(self, callables):
                        self._callables = callables

                    def __call__(self, *args, **kwargs):
                        return [c(*args, **kwargs) for c in self._callables]

            else:
                raise TypeError()

            new_subclass = super().__new__(mcls, subclass_name, (new_class, ), dict(Base.__dict__))

            return new_subclass

        return new_class

    @staticmethod
    def _is_sub_class(cls):
        return cls.__bases__ != (object, )

    def __call__(self, *args, flatten=True, **kwargs):
        if self._is_sub_class(self):
            return super().__call__(*args, **kwargs)

        if flatten:
            args = iterable.flatten(args)

        chain = []

        def build_chain_of_commands(entry):
            if callable(entry):
                return SingleCallable(entry)
            elif isinstance(entry, (list, set, tuple)):
                sub_cmds = []
                for sub in entry:
                    sub_cmds.append(build_chain_of_commands(sub))
                return MultiCallable(sub_cmds)

        for entry in args:
            chain.append(build_chain_of_commands(entry))

        if len(chain) == 1:
            return chain[0]
        elif len(chain) > 1:
            return MultiCallable(chain)
        else:
            raise TypeError("Need to pass at least one callable!")




class Pipeline(metaclass=CallableMeta, mode=Mode.PIPELINE):
    """
    Creates callables as a single pipeline.
    """


class Callable(metaclass=CallableMeta, mode=Mode.INDEPENDENT):
    """ Encapsulates multiple callables as independent entries.

    Args:
        abc (_type_): _description_
    """





class SingleCallable(Callable):
    """ Callable pattern.

    Args:
        abc (_type_): _description_
    """
    def __init__(self, callable: Callable = None):
        self._callable = callable

    def __call__(self, *args, **kwargs):
        return self._callable(*args, **kwargs)

    def __iter__(self):
        yield self._callable

    @property
    def name(self):
        if not inspect.isfunction(self._callable):
            # Class instances fall in this category
            return self._callable.__class__.__name__
        else:
            try:
                return self._callable.__name__
            except AttributeError:
                return "<lambda>"


    def __repr__(self):
        return f"Callable({self.name})"



class MultiCallable(Callable):
    """ Callable pattern.

    Args:
        abc (_type_): _description_
    """
    __call__ = abc.abstractmethod  # Provided by the metaclass
    __iter__ = abc.abstractmethod  # Provided by the metaclass for pipeline

    def __repr__(self):
        # TODO: This is messy, find away to clean it up.
        text = "\n".join([''] + [repr(c) for c in self._callables])
        text = "\n".join([''] + [f'    {t}' for t in text.splitlines()[1:]])

        return f"Callable({text})"

    # def __iter__(self):
    #     for c in self._callables:
    #         yield from c




'''