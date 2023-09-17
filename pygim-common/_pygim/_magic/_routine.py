# -*- coding: utf-8 -*-
'''
This module implmements class Routine.
'''

import sys
import types
import inspect
from collections.abc import Callable
from dataclasses import dataclass
from .._utils import has_instances
from .._error_msgs import type_error_msg

__all__ = ['Routine']

_LOCAL_STR = '.<locals>.'

class RoutineMeta(type):
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

    if sys.version_info[:2] < (3, 11):
        _FUNC_VARS.remove("__qualname__")

    def __call__(self, func):
        if self.__name__ == "Routine":
            return super(self.__class__, self).__call__(func)

        assert isinstance(func, Routine), type_error_msg(func, Routine)
        func_map = {name: getattr(func, name) for name in self._FUNC_VARS}
        assert has_instances(func_map, str)
        mutable_func = super(self.__class__, self).__call__(func_map)
        return mutable_func


@dataclass
class Routine(metaclass=RoutineMeta):
    """ A class that represents a routine (function or method).

    In Python, routine is a callable object (function or method). This class
    represents a routine and provides some useful methods to inspect and
    manipulate it.
    """
    _routine: Callable

    @property
    def owning_class_name(self):
        return self._code_obj_vars["__qualname__"].split('.')[-2]

    def takes_arguments(self):
        pass

    def has_parent_class(self):
        qualname = self._routine.__qualname__

        # nested functions have ``.<locals>.`` in middle of the their qualname to
        # indicate that.
        qualname = qualname.split(_LOCAL_STR)[-1]

        # Functions that have class parent has one dot in its name.
        # TODO: ensure that it is the case.
        if '.' not in qualname:
            return False

        return True


class MutableRoutine(Routine):
    """ This class represents a routine that can be modified.

    Parameters
    ----------
    Routine : _type_
        _description_
    """
    _func_map: dict

    _FUNC_NEW_SIG = dict(
        code="__code__",
        globals="__globals__",
        name="__name__",
        argdefs="__defaults__",
        closure="__closure__",
    )

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

    # TODO: This should be supported by gimmick.
    def assign_to_class(self, __class, __new_name=None):
        assert inspect.isclass(__class)

        code_obj = MutableRoutine(self._func_map["__code__"])
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
