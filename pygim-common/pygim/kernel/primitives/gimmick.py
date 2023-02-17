"""

"""

from collections.abc import MutableMapping
import abc
from types import FunctionType

import pygim.typing as t
import pygim.exceptions as e
from pygim.utils import overlaps

__all__ = ["Gim"]


_SUPPORTS = "__supports_traits__"
_EMPTY_BODY = b"d\x01S\x00"


def _is_dunder(attr):
    return attr.startswith('__') and attr.endswith('__')


def _meta_factory(name, gim_mcls, other_mcls):
    """ Create metaclass that is also an abstract class. """
    return type(name, (gim_mcls, other_mcls), {})


def _create_gim_abc_meta(mcls, name, bases, attrs):
    return _meta_factory("GimABCMeta", mcls, abc.ABCMeta)(name, (abc.ABC,) + bases, attrs)


def _is_valid_interface_func(func: t.Callable[[t.Any], t.Any]) -> bool:
    if not func.__code__.co_code == _EMPTY_BODY:
        return False
    return True


def _is_valid_interface(func: t.Callable[[t.Any], t.Any]) -> bool:
    if isinstance(func, property):
        valid_fget = func.fget and _is_valid_interface_func(func.fget)
        valid_fset = func.fset and _is_valid_interface_func(func.fset)
        valid_fdel = func.fdel and _is_valid_interface_func(func.fdel)

        valid_fget = True if valid_fget is None else valid_fget
        valid_fset = True if valid_fset is None else valid_fset
        valid_fdel = True if valid_fdel is None else valid_fdel

        return valid_fget and valid_fset and valid_fdel
    return _is_valid_interface_func(func)


def _create_interface_class(mcls, name, bases, attrs):
    for name, func in attrs.items():
        if name in _INJECT_ABC_MAP: continue
        if _is_dunder(name): continue
        if getattr(func, "__isabstractmethod__", False):
            continue
        if isinstance(func, FunctionType):
            attrs[name] = abc.abstractmethod(func)
        elif isinstance(func, classmethod):
            func = func.__func__
            attrs[name] = abc.abstractclassmethod(func)
        elif isinstance(func, staticmethod):
            func = func.__func__
            attrs[name] = abc.abstractstaticmethod(func)
        elif isinstance(func, property):
            attrs[name] = abc.abstractproperty(func)
        else:
            continue
            raise e.GimError(f"Unknown: {name}")

        if not _is_valid_interface(func):
            raise e.GimError(
                "Interface functions are intended to be empty!",
                "Use `abstract` if you need function to contain body.",
                )

    mcls = _create_gim_abc_meta(mcls, name, bases, attrs)
    return mcls


class Registry(MutableMapping):
    def __init__(self):
        self.__registered = dict()

    def register(self, name, func):
        self.__registered[name] = func

    def __getitem__(self, key):
        return self.__registered[key]

    def __delitem__(self, __key) -> None:
        raise NotImplementedError

    def __len__(self):
        return len(self.__registered)

    def __setitem__(self, __key, __value) -> None:
        self.__registered[__key] = __value

    def __iter__(self):
        yield from self.__registered


trait_registry = Registry()
trait_registry.update(
    interface=_create_interface_class,
    abc=_create_gim_abc_meta,

)

_INJECT_ABC_MAP = dict(
    abc=abc,
    abstractmethod=abc.abstractmethod,
    abstractclassmethod=abc.abstractclassmethod,
    abstractstaticmethod=abc.abstractstaticmethod,
    abstractproperty=abc.abstractproperty,
    )


class GimMeta(type):
    def __prepare__(name, bases, **kwargs):
        if overlaps(["interface", "abc"], kwargs):
            return _INJECT_ABC_MAP.copy()
        return {}

    def __new__(mcls, name, bases, attrs, **kwargs):
        for k, v in kwargs.items():
            if v:
                return trait_registry[k](mcls, name, bases, attrs)
        return super().__new__(mcls, name, bases, attrs)


class Gim(metaclass=GimMeta):
    """ Class that supports traits. """
    __supports_traits__ = ('interface', 'abstract')  # Support introspection somehow?
