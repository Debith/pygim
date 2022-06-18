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


def _get_module_name(depth: int = 2):
    try:
        return sys._getframe(depth).f_globals.get('__name__', '__main__')
    except (AttributeError, ValueError):
        return '__main__'


def _clone_function(co):
    trait = OrderedDict()

    trait["co_argcount"] = co.co_argcount
    trait["co_kwonlyargcount"] = co.co_kwonlyargcount
    if sys.version_info[:2] >= (3, 8):
        trait["co_posonlyargcount"] = co.co_posonlyargcount
    trait["co_nlocals"] = co.co_nlocals
    trait["co_stacksize"] = co.co_stacksize
    trait["co_flags"] = co.co_flags
    trait["co_code"] = co.co_code
    trait["co_consts"] = co.co_consts
    trait["co_names"] = co.co_names
    trait["co_varnames"] = co.co_varnames
    trait["co_filename"] = co.co_filename
    trait["co_name"] = co.co_name
    trait["co_firstlineno"] = co.co_firstlineno
    trait["co_lnotab"] = co.co_lnotab

    return trait


def _match_variable_names_with_target(code_obj, source_name: t.Text, target_name: t.Text):
    new_co_vars = _clone_function(code_obj)

    new_co_names = new_co_vars["co_names"]
    items = []
    for co_name in new_co_names:
        if not co_name.startswith(f'_{source_name}__'):
            items.append(co_name)
            continue

        items.append(f"_{target_name}__{co_name.split('__')[-1]}")
        print(items)

    new_co_vars["co_names"] = tuple(items)

    return types.CodeType(*new_co_vars.values())


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
    code_obj = source.__code__

    *start, class_name, func_name = source.__qualname__.split('.')
    new_code_obj = _match_variable_names_with_target(code_obj, class_name, target.__name__)
    new_func = types.FunctionType(new_code_obj, source.__globals__)
    new_func.__qualname__ = f'{".".join(start)}.{target.__name__}.{func_name}'.lstrip(".")
    new_func.__name__ = func_name
    new_func.__module__ = _get_module_name()
    new_bound_func = new_func.__get__(None, target)

    setattr(target, name, new_bound_func)