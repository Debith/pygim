# -*- coding: utf-8 -*-
'''
This module implmements class ``Interface``.
'''

import abc
from types import FunctionType
from dataclasses import dataclass, field

from _pygim._magic._gimmick import gimmick, gim_type
from _pygim._utils import format_dict
import _pygim._exceptions as e

__all__ = ['Interface', 'AbstractClass']

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


class GimABCError(e.GimError):
    """Base class for all errors raised by this module."""


def reraise(func):
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except GimABCError as exc:
            raise GimABCError(str(exc)) from None
    return wrapper


def _walk_bases(bases):
    for base in bases:
        yield base
        yield from _walk_bases(base.__bases__)


def _walk_methods(collection, *, check_func=lambda *x: True):
    if hasattr(collection, "__dict__"):
        collection = collection.__dict__
    for name, value in collection.items():
        if check_func(name, value):
            yield name, value


CONCRETE, ABSTRACT, ABSTRACT_WANNABE = object(), object(), object()


@dataclass
class Abstractions:
    """ Tracks all methods of a class and its bases, and whether they are
    concrete or abstract. """
    _ignore: set
    _methods: dict = field(default_factory=dict)

    def _extract_func(self, method):
        if isinstance(method, FunctionType):
            return method
        elif isinstance(method, classmethod):
            return method.__func__
        elif isinstance(method, staticmethod):
            return method.__func__
        elif isinstance(method, property):
            return method
        else:
            raise GimABCError(f"Unknown: {method.__name__}")

    def _is_abstractable(self, name, obj):
        if _is_dunder(name):
            return False

        if name in self._ignore:
            return False

        if not callable(obj) and not isinstance(obj, property):
            return False  # Callable objects are abstractable

        if getattr(self._extract_func(obj), "__isabstractmethod__", False):
            return False  # Already marked as abstract

        return True

    def _tag(self, method):
        if getattr(method, "__isabstractmethod__", False):
            return ABSTRACT
        if _is_valid_interface(self._extract_func(method)):
            return ABSTRACT_WANNABE
        return CONCRETE

    def _add_method(self, method_name, method):
        if method_name in self._ignore:
            return

        tag = self._tag(method)
        if method_name in self._methods:
            if tag is not CONCRETE:
                return

        self._methods[method_name] = tag

    def from_bases(self, *classes):
        for cls in classes:
            for method_name, method in _walk_methods(cls, check_func=self._is_abstractable):
                self._add_method(method_name, method)

    def from_namespace(self, namespace):
        for attr_name, value in _walk_methods(namespace, check_func=self._is_abstractable):
            self._add_method(attr_name, value)

    def __str__(self):
        def function_formatter(func):
            return func.lower()
        return format_dict(self._methods, indent=4,
                           value_formatter=function_formatter)

    @property
    def taggable(self):
        for method_name, method in self._methods.items():
            if method is ABSTRACT_WANNABE:
                yield method_name


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
        # Adding ``gimmick`` to bases here is important, because ensures
        # that MRO is constructed properly. A wrong MRO directly prevents
        # implementation of derived classes.
        if gimmick not in bases:
            bases += (gimmick, )

        if abc.ABC not in bases:
            return bases + (abc.ABC, )
        return bases

    @classmethod
    def _ensure_abstract_methods_and_properties(mcls, methods, attrs, allow_empty_body):
        for method_name in methods.taggable:
            method = attrs[method_name]
            if isinstance(method, FunctionType):
                method = abc.abstractmethod(method)
            elif isinstance(method, classmethod):
                method = abc.abstractclassmethod(method.__func__)
            elif isinstance(method, staticmethod):
                method = abc.abstractstaticmethod(method.__func__)
            elif isinstance(method, property):
                method = abc.abstractproperty(method.__func__)
            else:
                raise GimABCError(f"Unknown: {method.__name__}")

            if not allow_empty_body and not _is_valid_interface(method):
                raise GimABCError(
                    "Interface functions are intended to be empty! "
                    f"Use ``{mcls.__module__}.AbstractClass`` if you need "
                    "function to contain body.",
                    )

        return attrs

    @classmethod
    def _clean_attrs(mcls, attrs):
        # Remove all ABCs from attrs, they are only needed for making them
        # available in the namespace during (sub)class creation.
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
        if name in ("AbstractClass", "Interface"):
            return super().__new__(mcls, name, bases, mcls._clean_attrs(namespace))

        abstractions = Abstractions(set(mcls._INJECT_ABC_MAP))

        bases = mcls._ensure_abstract_bases(bases)
        attrs = mcls._clean_attrs(namespace)
        abstractions.from_bases(*bases)
        abstractions.from_namespace(namespace)
        attrs = mcls._ensure_abstract_methods_and_properties(abstractions, attrs, allow_empty_body)

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

    @classmethod
    def _is_interface_derived(mcls, bases):
        for cls in _walk_bases(bases):
            if cls is Interface:
                return True
        return False

    @classmethod
    def _has_abstract_methods(mcls, bases):
        for cls in _walk_bases(bases):
            if cls is Interface:
                continue
            if cls.__abstractmethods__:
                return True
        return False


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
