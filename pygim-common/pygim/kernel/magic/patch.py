# -*- coding: utf-8 -*-
"""
Utility functions that are useful to patch objects and classes.
"""

__all__ = ["transfer_ownership"]

from abc import ABCMeta
from dataclasses import dataclass
import sys
import typing as t
import types
import inspect

from ...utils import flatten

def has_instances(iterable, types, *, how=all):
    return how(isinstance(it, types) for it in iterable)


def format_dict(dct, *, indent=0):
    indention = " " * indent
    lines = [''] + [f"{indention}{key}={repr(value)}," for key, value in dct.items()] + ['']
    formatted_string = "\n".join(lines)

    return formatted_string


class MutableCodeObjectMeta(ABCMeta):
    _CO_OBJ_VARS = [
        "co_argcount",
        "co_kwonlyargcount",
        "co_posonlyargcount",
        "co_nlocals",
        "co_stacksize",
        "co_flags",
        "co_code",
        "co_consts",
        "co_names",
        "co_varnames",
        "co_filename",
        "co_name",
        "co_firstlineno",
        "co_lnotab",
    ]

    if sys.version_info[:2] < (3, 8):
        _CO_OBJ_VARS.remove("co_posonlyargcount")

    def __call__(self, code_obj):
        code_obj = {name: getattr(code_obj, name) for name in self._CO_OBJ_VARS}
        assert has_instances(code_obj, str)
        mutable_code_obj = super(self.__class__, self).__call__(code_obj)
        return mutable_code_obj


@dataclass
class MutableCodeObject(metaclass=MutableCodeObjectMeta):
    _code_map: dict

    def rename_owner(self, target_name):
        def modify(name):
            private_name = name.split('__')
            if len(private_name) != 2:
                return name

            return f"_{target_name}__{private_name[-1]}"

        self._code_map["co_names"] = tuple(map(modify, self._code_map["co_names"]))

    def __setitem__(self, key, value):
        self._code_map[key] = value

    def freeze(self):
        return types.CodeType(*self._code_map.values())

    def __repr__(self):
        return f"MutableCodeObject({format_dict(self._code_map, indent=4)})"


@dataclass
class MutableFuncObject:
    _func_obj: t.Callable

    @property
    def owning_class_name(self):
        return self._func_obj.__qualname__.split('.')[-2]

    def new_qualname(self, target):
        return f"{target.__qualname__}.{self._func_obj.__name__}"

    @property
    def function_name(self):
        return self._func_obj.__name__

    def _get_module_name(self, depth: int = 2):
        try:
            return sys._getframe(depth).f_globals.get('__name__', '__main__')
        except (AttributeError, ValueError):
            return '__main__'

    def __rshift__(self, target):
        assert inspect.isclass(target)
        assert not hasattr(target, self.function_name)

        code_obj = MutableCodeObject(self._func_obj.__code__)
        code_obj.rename_owner(target.__name__)
        new_func = types.FunctionType(code_obj.freeze(), self._func_obj.__globals__)
        new_func.__qualname__ = self.new_qualname(target)
        new_func.__name__ = self.function_name
        new_func.__module__ = self._func_obj.__module__
        new_bound_func = new_func.__get__(None, target)
        setattr(target, self.function_name, new_bound_func)


def transfer_ownership(target, *funcs):
    """ Transfer ownership of source object to target object.

    The point of transferring the ownership is to ensure that the
    target things it has belonged into that object right from the
    creation of the object. This is particularly useful with traits
    support.

    This is a low level function.

    Arguments:
        source: This can be callable [, class or instance]
        target: Target class to be updated.
    """
    assert inspect.isclass(target)

    for func in flatten(funcs):
        assert callable(func)
        func_obj = MutableFuncObject(func)
        func_obj >> target
