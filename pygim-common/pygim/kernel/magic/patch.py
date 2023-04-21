# -*- coding: utf-8 -*-
"""
Utility functions that are useful to patch objects and classes.
"""

__all__ = ["transfer_ownership"]

from abc import ABCMeta
from collections.abc import MutableMapping
from dataclasses import dataclass
import dis
import sys
import textwrap
import typing as t
import types
import inspect


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
class MutableCodeObject(MutableMapping, metaclass=MutableCodeObjectMeta):
    _code_map: dict

    def rename_owner(self, source_name, target_name):
        new_co_names = self["co_names"]
        items = []
        for co_name in new_co_names:
            if not co_name.startswith(f'_{source_name}__'):
                items.append(co_name)
                continue

            items.append(f"_{target_name}__{co_name.split('__')[-1]}")

        self["co_names"] = tuple(items)

    def freeze(self):
        return types.CodeType(*self.values())

    def __iter__(self):
        yield from self._code_map

    def __len__(self):
        return len(self._code_map)

    def __setitem__(self, __key, __value):
        self._code_map[__key] = __value

    def __getitem__(self, __key):
        return self._code_map[__key]

    def __delitem__(self, __key):
        del self._code_map[__key]

    def __repr__(self):
        return f"MutableCodeObject({format_dict(self._code_map, indent=4)})"



def _get_module_name(depth: int = 2):
    try:
        return sys._getframe(depth).f_globals.get('__name__', '__main__')
    except (AttributeError, ValueError):
        return '__main__'


def transfer_ownership(source, target, name=None):
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
    assert callable(source)
    assert inspect.isclass(target)

    name = name or source.__name__
    code_obj = MutableCodeObject(source.__code__)

    *start, class_name, func_name = source.__qualname__.split('.')
    new_code_obj = _match_variable_names_with_target(code_obj, class_name, target.__name__)
    new_func = types.FunctionType(new_code_obj, source.__globals__)
    new_func.__qualname__ = f'{".".join(start)}.{target.__name__}.{func_name}'.lstrip(".")
    new_func.__name__ = func_name
    new_func.__module__ = _get_module_name()
    new_bound_func = new_func.__get__(None, target)

    setattr(target, name, new_bound_func)