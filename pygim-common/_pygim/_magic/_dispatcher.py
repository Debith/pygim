# -*- coding: utf-8 -*-
'''
This module implmements class Dispatcher.
'''

from dataclasses import dataclass, field

__all__ = ['Dispatcher']


class DispatcherMeta(type):
    pass


def _tuplify(obj):
    if not isinstance(obj, tuple):
        return tuple([obj])
    return obj

def _arg_identifier(arg):
    if isinstance(arg, type):
        return type
    return lambda v: v


@dataclass
class Dispatcher(metaclass=DispatcherMeta):
    '''
    '''
    __callable: object
    __registry: dict = field(default_factory=dict)
    __arg_count: int = None
    __args: tuple = None

    def register(self, *specs):
        if not self.__args:
            self.__args = tuple(_arg_identifier(a) for a in specs)

        # TODO: verify length
        def __inner_register(func):
            self.__registry[specs] = func
        return __inner_register

    def __call__(self, *args, **kwargs):
        its_type = tuple(self.__args[i](args[i]) for i in range(len(self.__args)))
        return self.__registry[its_type](*args, **kwargs)


dispatch = Dispatcher
