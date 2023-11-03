# -*- coding: utf-8 -*-
"""
Utility functions that are useful to patch objects and classes.
"""

__all__ = ["MutableCodeObject", "MutableFuncObject"]

from abc import ABCMeta
from dataclasses import dataclass
import sys
import types
import inspect
import types

from .._utils import has_instances, format_dict, TraitFunctions, is_subset
from .._error_msgs import type_error_msg

PY37, PY38, PY39, PY310, PY311 = (3, 7), (3, 8), (3, 9), (3, 10), (3, 11)
____ = _____ = ()

_PY_CODE_ARGS = dict(
    argcount        = (PY37, PY38, PY39, PY310, PY311),
    posonlyargcount = (____, PY38, PY39, PY310, PY311),
    kwonlyargcount  = (PY37, PY38, PY39, PY310, PY311),
    nlocals         = (PY37, PY38, PY39, PY310, PY311),
    stacksize       = (PY37, PY38, PY39, PY310, PY311),
    flags           = (PY37, PY38, PY39, PY310, PY311),
    codestring      = (PY37, PY38, PY39, PY310, PY311),
    constants       = (PY37, PY38, PY39, PY310, PY311),
    names           = (PY37, PY38, PY39, PY310, PY311),
    varnames        = (PY37, PY38, PY39, PY310, PY311),
    filename        = (PY37, PY38, PY39, PY310, PY311),
    name            = (PY37, PY38, PY39, PY310, PY311),
    qualname        = (____, ____, ____, _____, PY311),
    firstlineno     = (PY37, PY38, PY39, PY310, PY311),
    lnotab          = (PY37, PY38, PY39, _____, _____),
    linetable       = (____, ____, ____, PY310, PY311),
    exceptiontable  = (____, ____, ____, _____, PY311),
    freevars        = (PY37, PY38, PY39, PY310, PY311),
    cellvars        = (PY37, PY38, PY39, PY310, PY311),
)

_CODE_OBJECT_VARS = dict(
    co_argcount         = (PY37, PY38, PY39, PY310, PY311),
    co_cellvars         = (PY37, PY38, PY39, PY310, PY311),
    co_code             = (PY37, PY38, PY39, PY310, PY311),
    co_consts           = (PY37, PY38, PY39, PY310, PY311),
    co_exceptiontable   = (____, ____, ____, _____, PY311),
    co_filename         = (PY37, PY38, PY39, PY310, PY311),
    co_firstlineno      = (PY37, PY38, PY39, PY310, PY311),
    co_flags            = (PY37, PY38, PY39, PY310, PY311),
    co_freevars         = (PY37, PY38, PY39, PY310, PY311),
    co_kwonlyargcount   = (PY37, PY38, PY39, PY310, PY311),
    co_linetable        = (____, ____, ____, PY310, PY311),
    co_lnotab           = (PY37, PY38, PY39, PY310, PY311),
    co_name             = (PY37, PY38, PY39, PY310, PY311),
    co_names            = (PY37, PY38, PY39, PY310, PY311),
    co_nlocals          = (PY37, PY38, PY39, PY310, PY311),
    co_posonlyargcount  = (____, PY38, PY39, PY310, PY311),
    co_qualname         = (____, ____, ____, _____, PY311),
    co_stacksize        = (PY37, PY38, PY39, PY310, PY311),
    co_varnames         = (PY37, PY38, PY39, PY310, PY311),
)

_ARGS_TO_VARS = dict(
    argcount         = "co_argcount",
    posonlyargcount  = "co_posonlyargcount",
    kwonlyargcount   = "co_kwonlyargcount",
    nlocals          = "co_nlocals",
    stacksize        = "co_stacksize",
    flags            = "co_flags",
    codestring       = "co_code",
    constants        = "co_consts",
    names            = "co_names",
    varnames         = "co_varnames",
    filename         = "co_filename",
    name             = "co_name",
    qualname         = "co_qualname",
    firstlineno      = "co_firstlineno",
    lnotab           = "co_lnotab",
    linetable        = "co_linetable",
    exceptiontable   = "co_exceptiontable",
    freevars         = "co_freevars",
    cellvars         = "co_cellvars",
)

_CUR_PY_VER = sys.version_info[:2]
_CUR_CODETYPE_VARS = [k for k, v in _CODE_OBJECT_VARS.items() if _CUR_PY_VER in v]
_CUR_CODETYPE_ARGS = [k for k, v in _PY_CODE_ARGS.items() if _CUR_PY_VER in v]
_CUR_CODETYPE_ARGS_INDEX = list(_CUR_CODETYPE_ARGS)
_CUR_ARGS_TO_VARS = {k:v for k,v in _ARGS_TO_VARS.items() if k in _CUR_CODETYPE_ARGS}

assert is_subset(_CUR_CODETYPE_VARS, dir(types.CodeType))
assert len(_CUR_ARGS_TO_VARS) == len(_CUR_CODETYPE_ARGS)
assert len(_CUR_CODETYPE_ARGS_INDEX) == len(_CUR_CODETYPE_ARGS)


class MutableCodeObjectMeta(ABCMeta):
    def __call__(self, code_obj):
        code_map = {name: getattr(code_obj, name) for name in _CUR_CODETYPE_VARS}
        assert has_instances(code_map, str)
        mutable_code_obj = super(self.__class__, self).__call__(code_map)
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

    def __iter__(self):
        yield from self._code_map

    def __setitem__(self, key, value):
        self._code_map[key] = value

    def __getitem__(self, key):
        return self._code_map[key]

    def freeze(self):
        try:
            args = {k: self._code_map[v] for k, v in _CUR_ARGS_TO_VARS.items()}
            return types.CodeType(*args.values())
        except TypeError as e:
            raise

    def __repr__(self):
        return f"{self.__class__.__name__}({format_dict(self._code_map, indent=4)})"


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

    if sys.version_info[:2] < (3, 11):
        _FUNC_VARS.remove("__qualname__")

    def __call__(self, func):
        assert isinstance(func, TraitFunctions), type_error_msg(func, TraitFunctions)
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

    def freeze(self):
        assert self._func_map["__name__"]

        kwargs = {k: self._func_map[v] for k, v in self.__class__._FUNC_NEW_SIG.items()}
        new_func = types.FunctionType(**kwargs)
        self._copy_field(new_func, "__kwdefaults__")
        self._copy_field(new_func, "__annotations__")
        self._copy_field(new_func, "__dict__")
        return new_func

    def _copy_field(self, new_func, field_name):
        if field_name not in self._func_map:
            return
        if self._func_map[field_name] is None:
            return
        setattr(new_func, field_name, self._func_map[field_name].copy())

    def assign_to_class(self, __class, __new_name=None):
        assert inspect.isclass(__class)

        code_obj = MutableCodeObject(self._func_map["__code__"])
        code_obj.rename_owner(__class.__name__)
        self._func_map["__code__"] = code_obj.freeze()

        if __new_name is not None:
            self._func_map["__name__"] = __new_name

        new_func = self.freeze()
        new_func.__qualname__ = self.new_qualname(__class)
        new_func.__pygim_parent__ = __class
        new_bound_func = new_func.__get__(None, __class)
        setattr(__class, self.function_name, new_bound_func)

        return self

    __rshift__ = assign_to_class
