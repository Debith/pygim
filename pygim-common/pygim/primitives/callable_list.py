    # -*- coding: utf-8 -*-
"""

"""

import abc
from ast import Call
import typing as t
from dataclasses import dataclass
from pygim.utils import iterable
import enum
import inspect

__all__ = ['Callable']


class Mode(enum.IntEnum):
    INDEPENDENT = enum.auto()
    PIPELINE = enum.auto()


def composite_pattern(component_name, abstract_methods=None):
    class LeafBase:
        pass

    class CompositeBase:
        pass

    class Component(abc.ABC):
        method = abc.abstractmethod

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


class CallableMetaMeta(type):
    def __call__(self, *args, **kwargs):
        return super().__call__(*args, **kwargs)


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