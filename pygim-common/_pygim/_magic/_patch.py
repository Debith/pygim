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

from .._utils import has_instances, format_dict, type_error_msg, TraitFunctions

# Python 3.7
#  |  code(argcount, kwonlyargcount, nlocals, stacksize, flags, codestring,
#  |        constants, names, varnames, filename, name, firstlineno,
#  |        lnotab[, freevars[, cellvars]])
#  |
#  |  co_argcount
#  |  co_cellvars
#  |  co_code
#  |  co_consts
#  |  co_filename
#  |  co_firstlineno
#  |  co_flags
#  |  co_freevars
#  |  co_kwonlyargcount
#  |  co_lnotab
#  |  co_name
#  |  co_names
#  |  co_nlocals

# Python 3.8
#  |  code(argcount, posonlyargcount, kwonlyargcount, nlocals, stacksize,
#  |        flags, codestring, constants, names, varnames, filename, name,
#  |        firstlineno, lnotab[, freevars[, cellvars]])
#
#  |  co_argcount
#  |  co_cellvars
#  |  co_code
#  |  co_consts
#  |  co_filename
#  |  co_firstlineno
#  |  co_flags
#  |  co_freevars
#  |  co_kwonlyargcount
#  |  co_lnotab
#  |  co_name
#  |  co_names
#  |  co_nlocals
#  |  co_posonlyargcount
#  |  co_stacksize
#  |  co_varnames

# Python 3.9
#  |  code(argcount, posonlyargcount, kwonlyargcount, nlocals, stacksize,
#  |        flags, codestring, constants, names, varnames, filename, name,
#  |        firstlineno, lnotab[, freevars[, cellvars]])
#
#  |  co_argcount
#  |  co_cellvars
#  |  co_code
#  |  co_consts
#  |  co_filename
#  |  co_firstlineno
#  |  co_flags
#  |  co_freevars
#  |  co_kwonlyargcount
#  |  co_lnotab
#  |  co_name
#  |  co_names
#  |  co_nlocals
#  |  co_posonlyargcount
#  |  co_stacksize
#  |  co_varnames

# Python 3.10
#  |  code(argcount, posonlyargcount, kwonlyargcount, nlocals, stacksize, flags, codestring, constants, names, varnames, filename, name, firstlineno, linetable, freevars=(), cellvars=(), /)
#  |  co_argcount
#  |  co_cellvars
#  |  co_code
#  |  co_consts
#  |  co_filename
#  |  co_firstlineno
#  |  co_flags
#  |  co_freevars
#  |  co_kwonlyargcount
#  |  co_linetable
#  |  co_lnotab
#  |  co_name
#  |  co_names
#  |  co_nlocals
#  |  co_posonlyargcount
#  |  co_stacksize
#  |  co_varnames

#  Python 3.11
#  |  code(argcount, posonlyargcount, kwonlyargcount, nlocals, stacksize, flags, codestring, constants, names, varnames, filename, name, qualname, firstlineno, linetable, exceptiontable, freevars=(), cellvars=(), /)
#  |  co_argcount
#  |  co_cellvars
#  |  co_code
#  |  co_consts
#  |  co_exceptiontable
#  |  co_filename
#  |  co_firstlineno
#  |  co_flags
#  |  co_freevars
#  |  co_kwonlyargcount
#  |  co_linetable
#  |  co_lnotab
#  |  co_name
#  |  co_names
#  |  co_nlocals
#  |  co_posonlyargcount
#  |  co_qualname
#  |  co_stacksize
#  |  co_varnames

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
        _CO_OBJ_VARS.remove("co_qualname")
        ix = _CO_OBJ_VARS.index("co_linetable")
        _CO_OBJ_VARS[ix] = "co_lnotab"

    if sys.version_info[:2] < (3, 8):
        _CO_OBJ_VARS.remove("co_posonlyargcount")

    def __call__(self, code_obj):
        lno_tab = code_obj.co_lnotab
        code_map = {name: getattr(code_obj, name) for name in self._CO_OBJ_VARS}
        code_map["co_lnotab"] = lno_tab
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
        return types.CodeType(*self._code_map.values())

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
