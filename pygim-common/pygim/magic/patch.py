# -*- coding: utf-8 -*-
"""
Utility functions that are useful to patch objects and classes.
"""

__all__ = ["transfer_ownership"]

import dis
import sys
import typing as t
import types
import inspect

from collections import OrderedDict

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


def _get_module_name(depth: int = 2):
    try:
        return sys._getframe(depth).f_globals.get('__name__', '__main__')
    except (AttributeError, ValueError):
        return '__main__'


def _extract_co_vars(co):
    return {v: getattr(co, v) for v in _CO_OBJ_VARS}


def _match_variable_names_with_target(code_obj, source_name: t.Text, target_name: t.Text):
    new_co_names = code_obj["co_names"]
    items = []
    for co_name in new_co_names:
        if not co_name.startswith(f'_{source_name}__'):
            items.append(co_name)
            continue

        items.append(f"_{target_name}__{co_name.split('__')[-1]}")

    code_obj["co_names"] = tuple(items)

    return types.CodeType(*code_obj.values())


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
    code_obj: t.Mapping[t.Text, t.Any] = _extract_co_vars(source.__code__)

    *start, class_name, func_name = source.__qualname__.split('.')
    new_code_obj = _match_variable_names_with_target(code_obj, class_name, target.__name__)
    new_func = types.FunctionType(new_code_obj, source.__globals__)
    new_func.__qualname__ = f'{".".join(start)}.{target.__name__}.{func_name}'.lstrip(".")
    new_func.__name__ = func_name
    new_func.__module__ = _get_module_name()
    new_bound_func = new_func.__get__(None, target)

    setattr(target, name, new_bound_func)