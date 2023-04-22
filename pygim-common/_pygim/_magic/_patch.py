# -*- coding: utf-8 -*-
"""
Utility functions that are useful to patch objects and classes.
"""

__all__ = ["MutableCodeObject", "MutableFuncObject"]

from abc import ABCMeta
from dataclasses import dataclass
import sys
import typing as t
import types
import inspect

from .._utils import has_instances, format_dict

class MutableCodeObjectMeta(ABCMeta):
    _CO_OBJ_VARS = [
        "co_argcount",
        "co_posonlyargcount",
        "co_kwonlyargcount",
        "co_nlocals",
        "co_stacksize",
        "co_flags",
        "co_code",
        "co_consts",
        "co_names",
        "co_varnames",
        "co_filename",
        "co_name",
        "co_qualname",
        "co_firstlineno",
        "co_linetable",
        "co_exceptiontable",
        "co_freevars",
        "co_cellvars",
    ]

    if sys.version_info[:2] < (3, 11):
        _CO_OBJ_VARS.remove("co_exceptiontable")

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


class MutableFuncObjectMeta(type):
    _FUNC_VARS = [
        "__closure__",
        "__code__",
        "__defaults__",
        "__kwdefaults__",
        "__globals__",
        "__module__",
        "__name__",
        "__qualname__",
        "__doc__",
        ]

    _FUNC_NEW_SIG = dict(
        code="__code__",
        globals="__globals__",
        name="__name__",
        argdefs="__defaults__",
        closure="__closure__",
    )

    def __call__(self, func):
        assert isinstance(func, types.FunctionType)
        func_map = {name: getattr(func, name) for name in self._FUNC_VARS}
        assert has_instances(func_map, str)
        mutable_func = super(self.__class__, self).__call__(func_map)
        return mutable_func

@dataclass
class MutableFuncObject(metaclass=MutableFuncObjectMeta):
    _func_map: dict

    @property
    def owning_class_name(self):
        return self._func_map["__qualname__"].split('.')[-2]

    def new_qualname(self, target):
        return f"{target.__qualname__}.{self._func_map['__name__']}"

    @property
    def function_name(self):
        return self._func_map["__name__"]

    def _get_module_name(self, depth: int = 2):
        try:
            return sys._getframe(depth).f_globals.get('__name__', '__main__')
        except (AttributeError, ValueError):
            return '__main__'

    def __rshift__(self, target):
        assert inspect.isclass(target)
        assert not hasattr(target, self.function_name)

        code_obj = MutableCodeObject(self._func_map["__code__"])
        code_obj.rename_owner(target.__name__)
        self._func_map["__code__"] = code_obj.freeze()

        kwargs = {k: self._func_map[v] for k, v in self.__class__._FUNC_NEW_SIG.items()}
        new_func = types.FunctionType(**kwargs)
        new_func.__qualname__ = self.new_qualname(target)
        new_bound_func = new_func.__get__(None, target)
        setattr(target, self.function_name, new_bound_func)
