# -*- coding: utf-8 -*-
'''
This module implmements class gimmick.
'''

from traceback import format_stack
from types import FunctionType
from functools import singledispatchmethod

from ._patch import MutableFuncObject
from .._utils import type_error_msg, flatten

__all__ = ['gimmick']


class GimTypeMeta(type):
    def __prepare__(*args):
        return dict(__pygim_traits__={})

    def __lshift__(self, other):
        print("thing")


class gim_type(type, metaclass=GimTypeMeta):
    """
    A metaclass that provides dynamic trait behavior for Python classes.

    Parameters
    ----------
    type : type
        The type of the class being created.
    metaclass : gim_type
        The metaclass for the `gim_type` class.

    Methods
    -------
    __lshift__
        Adds a trait to the class using `Class << (function or class)` notation.
    __setattr__
        Sets an attribute on the class. `Class.function_name = func` notation.

    Notes
    -----
    The `gim_type` metaclass provides dynamic trait behavior for Python classes, allowing you
    to add new functionality to a class at runtime. This can be useful in cases where you need
    to extend the functionality of a class that you don't have control over, or when you want
    to add behavior to a class in a flexible and dynamic way.
    """
    __slots__ = ()

    def __prepare__(*args):
        return dict(__pygim_traits__={})

    def __new__(mcls, name, bases=(), namespace=None, **kwargs):
        namespace = namespace or {}
        try:
            if gimmick not in bases:
                bases += (gimmick, )
        except NameError:
            return super().__new__(mcls, name, bases, namespace)
        return super().__new__(mcls, name, bases, namespace or {})

    def __init__(cls, name, bases=(), namespace=None):
        """"""
        super().__init__(name, bases, namespace)

    def __call__(self, *args, **kwargs):
        if self is gimmick:
            raise NotImplementedError()
        return super().__call__(*args, **kwargs)

    def __record_trait_info(cls, trait):
        """ Include trait information only in debug mode."""
        # NOTE: Executing `python -O` skips this step.
        if __debug__:
            traitinfo = f"{trait.__module__}.{trait.__qualname__}"
            lines = []
            for line in format_stack():
                if "_pygim" in line:
                    break
                lines.append(line.split('\n')[0].strip())
            fileinfo = f"{trait.__code__.co_filename}:{trait.__code__.co_firstlineno}"

            cls.__pygim_traits__[traitinfo] = dict(definition=fileinfo, traceback=lines)

    @singledispatchmethod
    def __lshift__(cls, other):
        raise TypeError(type_error_msg(other, FunctionType))

    @__lshift__.register(list)
    @__lshift__.register(tuple)
    def __lshift_iterable__(cls, _iterable):
        for obj in flatten(_iterable):
            cls << obj
        return cls

    @__lshift__.register(FunctionType)
    def __lshift_func__(cls, _func):
        cls.__do_setattr(_func, _func.__name__)
        return cls

    @__lshift__.register(type)
    def __lshift_class__(cls, _class):
        new_funcs = [f for f in _class.__dict__.values() if isinstance(f, FunctionType)]
        for func in new_funcs:
            cls << func
        return cls

    @singledispatchmethod  # TODO: Work singledispatch method
    def __do_setattr(cls, _value, _name):
        super().__setattr__(_name, _value)

    @__do_setattr.register(FunctionType)
    def __do_setattr_func(cls, _value, _name):
        # This condition ensures that functions that already assigned
        # to this class won't be done so again, as it will lead to
        # infinite recursion loop.
        # TODO: Maybe functions that are already assigned should be
        #       converted to special function types..?
        if getattr(_value, "__pygim_parent__", None) is cls:
            cls.__record_trait_info(_value)
            return super().__setattr__(_name, _value)
        MutableFuncObject(_value).assign_to_class(cls, _name)

    def __setattr__(cls, _name, _value):
        cls.__do_setattr(_value, _name)


class gimmick(metaclass=gim_type):
    """
    A Python class for dynamically adding behavior to existing classes.

    Notes
    -----
    The `gimmick` object can be used to attach functions to an existing class at runtime,
    providing a way to add behavior to a class without modifying its original code. This can
    be useful in cases where you need to add functionality to a `gimmick` class that you don't
    have control over, or when you want to add behavior to a class in a flexible and dynamic way.

    Attributes
    ----------
    __pygim_traits__
        Contains information about any dynamically assigned function or class trait. This dictionary
        holds details about the location where the trait was defined, as well as a traceback to the
        location where the trait was assigned to the class.

    Syntax
    ------
    The `gimmick` object can be used to attach functions to an existing class using either
    the `<<` operator or dot notation:

    ExampleClass << function_name
    ExampleClass.function_name = function_name

    Examples
    --------
    Here's an example of using the `gimmick` object to add behavior to a class:

    >>> class ExampleObject(gimmick):
    ...     def __init__(self):
    ...         self.public = 1
    ...
    ...     def original(self):
    ...         return self.public
    ...
    >>> def new_method(self):
    ...     return self.public + 1
    ...
    >>> ExampleObject << new_method
    >>> obj = ExampleObject()
    >>> obj.new_method()
    2

    Limitations
    -----------
    - The `gimmick` object is not compatible with classes that use metaclasses or have
      complex inheritance hierarchies.
    - The `gimmick` object can potentially introduce side effects or unexpected behavior
      if used improperly, so it's important to test the object thoroughly before using it
      in production code.
    """
    __slots__ = ()

    def __repr__(self):
        return f"{self.__class__.__name__}(0x{id(self)})"
