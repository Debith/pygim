# -*- coding: utf-8 -*-
'''
This module implmements class ``interface``.
'''

import abc
from collections.abc import Iterable
from dataclasses import dataclass, field
from types import FunctionType
from typing import TYPE_CHECKING

from _pygim._magic._gimmick import gimmick, gim_type
import _pygim._exceptions as e

__all__ = ['interface']

def __empty_body__():
    pass
_EMPTY_BODY = __empty_body__.__code__.co_code
del __empty_body__


def _is_dunder(attr):
    return attr.startswith('__') and attr.endswith('__')


def _is_valid_interface_func(func) -> bool:
    if not func.__code__.co_code == _EMPTY_BODY:
        return False
    return True


def _is_valid_interface(func) -> bool:
    if isinstance(func, property):
        valid_fget = func.fget and _is_valid_interface_func(func.fget)
        valid_fset = func.fset and _is_valid_interface_func(func.fset)
        valid_fdel = func.fdel and _is_valid_interface_func(func.fdel)

        valid_fget = True if valid_fget is None else valid_fget
        valid_fset = True if valid_fset is None else valid_fset
        valid_fdel = True if valid_fdel is None else valid_fdel

        return valid_fget and valid_fset and valid_fdel
    return _is_valid_interface_func(func)


def _walk_bases(bases):
    for base in bases:
        yield base
        yield from _walk_bases(base.__bases__)


class GimABCError(e.GimError):
    """Base class for all errors raised by this module."""


def reraise(func):
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except GimABCError as exc:
            raise GimABCError(str(exc)) from None
    return wrapper


class InterfaceMeta(gim_type, abc.ABCMeta):
    '''
    '''
    _INJECT_ABC_MAP = dict(
        abc=abc,
        abstractmethod=abc.abstractmethod,
        abstractclassmethod=abc.abstractclassmethod,
        abstractstaticmethod=abc.abstractstaticmethod,
        abstractproperty=abc.abstractproperty,
        )

    @classmethod
    def _ensure_abstract_bases(mcls, bases):
        if gimmick not in bases:
            bases += (gimmick, )
        if not bases:
            return (abc.ABC, )
        elif abc.ABC in bases:
            return bases
        else:
            return bases + (abc.ABC, )

    @classmethod
    def _ensure_abstract_methods_and_properties(mcls, attrs, allow_empty_body):
        for attr_name, attr_value in attrs.items():
            if attr_name in mcls._INJECT_ABC_MAP: continue
            if _is_dunder(attr_name): continue
            if getattr(attr_value, "__isabstractmethod__", False):
                continue
            if isinstance(attr_value, FunctionType):
                attrs[attr_name] = abc.abstractmethod(attr_value)
            elif isinstance(attr_value, classmethod):
                attr_value = attr_value.__func__
                attrs[attr_name] = abc.abstractclassmethod(attr_value)
            elif isinstance(attr_value, staticmethod):
                attr_value = attr_value.__func__
                attrs[attr_name] = abc.abstractstaticmethod(attr_value)
            elif isinstance(attr_value, property):
                attrs[attr_name] = abc.abstractproperty(attr_value)
            else:
                raise GimABCError(f"Unknown: {attr_name}")

            if not allow_empty_body and not _is_valid_interface(attr_value):
                raise GimABCError(
                    "Interface functions are intended to be empty! "
                    f"Use ``{mcls.__module__}.AbstractClass`` if you need function "
                    "to contain body.",
                    )

        return attrs

    @classmethod
    def _clean_attrs(mcls, attrs):
        attrs = {
            k: v for k, v in attrs.items() if k not in mcls._INJECT_ABC_MAP
            }

        return attrs

    @classmethod
    def __prepare__(cls, name, bases, **_):
        mapping = super().__prepare__(name, bases)
        mapping.update(cls._INJECT_ABC_MAP)

        return mapping

    @classmethod
    def __dir__(cls):
        return super().__dir__()

    @reraise
    def __new__(mcls, name, bases=(), namespace=None, *,
                allow_empty_body=False, **kwargs):
        if name in ("Abstract", "Interface"):
            return super().__new__(mcls, name, bases, mcls._clean_attrs(namespace))

        bases = mcls._ensure_abstract_bases(bases)
        attrs = mcls._clean_attrs(namespace)
        if Interface in bases:
            attrs = mcls._ensure_abstract_methods_and_properties(attrs, allow_empty_body)

        cls = super().__new__(mcls, name, bases, attrs)
        assert gimmick in cls.__bases__
        return cls

    def __call__(self, *args, **kwargs):
        if self is Interface:
            raise NotImplementedError()

        # If Interface is found in immediate bases, but no abstract methods
        # are found, then it is still an interface.
        if Interface in self.__bases__ and not self.__abstractmethods__:
            raise GimABCError("Can't instantiate interface!")

        try:
            return super().__call__(*args, **kwargs)
        except TypeError:
            raise GimABCError(
                f"Can't instantiate interface ``{self.__name__}`` "
                f"with abstract methods: {', '.join(sorted(self.__abstractmethods__))}"
                ) from None


class Interface(metaclass=InterfaceMeta, allow_method_body=False):
    """
    Represents a strict interface class in Python.

    An interface, as defined in this context, is a contract for other classes to implement.
    It contains only abstract methods without any implementation. Subclasses of an Interface
    must provide an implementation for all abstract methods defined in the interface.

    In this implementation, the Interface class does not allow method bodies. Any attempt to
    include implementation details in the methods of an Interface will result in a TypeError.

    Examples
    --------
    >>> class MyInterface(Interface):
    ...     def my_method(self):
    ...         pass

    Notes
    -----
    The Interface class is used to define a set of methods that the implementing class must
    provide. It's similar to interfaces in languages like Java, where it's used to define
    a contract without dictating the exact method implementation.

    See Also
    --------
    Abstract : For creating abstract classes with partial method implementations.
    """


class AbstractClass(metaclass=InterfaceMeta, allow_method_body=True):
    """
    Represents an abstract class in Python.

    An abstract class, unlike an interface, allows and may contain method implementations.
    Subclasses can inherit from an abstract class and override or extend the provided methods.
    However, similar to an interface, an abstract class cannot be instantiated on its own.

    In this implementation, the Abstract class allows method bodies, enabling partial or full
    implementation of methods, which subclasses can utilize or override.

    Examples
    --------
    >>> class MyAbstract(AbstractClass):
    ...     def implemented_method(self):
    ...         print("This method is implemented.")
    ...     def abstract_method(self):
    ...         pass

    Notes
    -----
    Abstract classes in Python are similar to those in other object-oriented languages. They
    provide a way to define default behavior for subclasses and to enforce certain methods to
    be implemented by any non-abstract subclass.

    The key distinction between an 'Abstract' class and an 'Interface' is that the former
    allows for method implementations, while the latter does not.

    See Also
    --------
    Interface : For creating strict interface classes without method implementations.
    """
