"""

"""

import abc
from types import FunctionType

import pygim.typing as t
import pygim.exceptions as e

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
            raise e.GimError(f"Unknown: {name}")

        if not _is_valid_interface(func):
            raise e.GimError(
                "Interface functions are intended to be empty!",
                "Use `abstract` if you need function to contain body.",
                )

    mcls = _create_gim_abc_meta(mcls, name, bases, attrs)
    return mcls


class MetaDict(dict):
    def __init__(self):
        print('test')
    def __getitem__(self, key):
        return super().__getitem__(key)


class GimMeta(type):

    def __prepare__(name, bases, **kwargs):
        return MetaDict()

    def __new__(mcls, name, bases, attrs, **kwargs):
        if "interface" in kwargs:
            return _create_interface_class(mcls, name, bases, attrs)
        if "abc" in kwargs:
            return _create_gim_abc_meta(mcls, name, bases, attrs)
        return super().__new__(mcls, name, bases, attrs)


class Gim(metaclass=GimMeta):
    """ Class that supports traits. """
    __supports_traits__ = ('interface', 'abstract')  # Support introspection somehow?
