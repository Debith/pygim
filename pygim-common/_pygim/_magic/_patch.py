# -*- coding: utf-8 -*-
"""
Utility functions that are useful to patch objects and classes.
"""

__all__ = ["MutableCodeObject"]

from abc import ABCMeta
from dataclasses import dataclass
import sys
import types
import types

from .._utils import has_instances, format_dict, is_subset

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
