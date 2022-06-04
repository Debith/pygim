# -*- coding: utf-8 -*-
"""
This module contains enhanced enum creation.
"""

import enum
import inspect
import pathlib


class InvokerFrame:
    def __init__(self, frame):
        self.__frame = frame

    def find_assignment_variable(self, search_class, index=2):
        code_context = [l for l in self.__frame.code_context if search_class in l][0]
        name = code_context.split('=')[0].strip()

        return name

    @property
    def module_name(self):
        name = self.__frame.frame.f_locals['__name__']
        if name == "__main__":
            filename = self.__frame.frame.f_locals['__file__']
            module_name = pathlib.Path(filename).stem
            return module_name
        return name


class CallStack:
    def __init__(self, func_name):
        self._stack = inspect.stack()
        funcs = [f.function for f in self._stack]
        self.__cur_idx = funcs.index(func_name)

    def __getitem__(self, index):
        return InvokerFrame(self._stack[self.__cur_idx + index])


class GimEnumMeta(enum.EnumMeta):
    def __create_new_enum(self, values):
        stack = CallStack("__create_new_enum")
        frame = stack[2]

        class_name = frame.find_assignment_variable('GimEnum')
        module_name = frame.module_name
        qualname = f"{module_name}.{class_name}"
        attrs = {t.upper(): t.lower() for t in values}

        enum_class = self._create_(class_name, attrs, module=module_name, qualname=qualname)

        return enum_class

    def __call__(self, *args, **kwargs):
        if isinstance(args[0], (tuple, list)):
            return self.__create_new_enum(args[0])
        else:
            clazz = super().__new__(self, *args)
            return clazz


class GimEnum(enum.Enum, metaclass=GimEnumMeta):
    """ Enumeration that can be created as one-liner and as normal class definition. """
