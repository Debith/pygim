# -*- coding: utf-8 -*-
'''
This module implmements class GimObject.
'''

from types import FunctionType
from functools import singledispatchmethod

from ._patch import MutableFuncObject
from ._traits import transfer_ownership
from .._utils import complain_type, flatten

__all__ = ['GimObject']


class GimObjectMeta(type):
    __slots__ = ()

    def __new__(mcls, name, bases=(), namespace=None, **kwargs):
        namespace = namespace or {}
        try:
            if gim_object not in bases:
                bases += (gim_object, )
        except NameError:
            return super().__new__(mcls, name, bases, namespace)
        return super().__new__(mcls, name, bases, namespace or {})

    def __init__(cls, name, bases=(), namespace=None):
        """"""
        super().__init__(name, bases, namespace)

    def __call__(self, *args, **kwargs):
        if self is GimObject:
            raise NotImplementedError()
        return super().__call__(*args, **kwargs)

    @singledispatchmethod
    def __lshift__(cls, other):
        raise TypeError(complain_type(other, FunctionType))

    @__lshift__.register(list)
    @__lshift__.register(tuple)
    def __lshift_iterable__(cls, _iterable):
        for obj in flatten(_iterable):
            cls << obj
        return cls

    @__lshift__.register(FunctionType)
    def __lshift_func__(cls, _func):
        cls.__do_setattr(_func, _func.__name__)
        return cls

    @__lshift__.register(type)
    def __lshift_class__(cls, _class):
        new_funcs = [f for f in _class.__dict__.values() if isinstance(f, FunctionType)]
        for func in new_funcs:
            cls << func
        return cls

    @singledispatchmethod  # TODO: Work singledispatch method
    def __do_setattr(cls, _value, _name):
        super().__setattr__(_name, _value)

    @__do_setattr.register(FunctionType)
    def __do_setattr_func(cls, _value, _name):
        # This condition ensures that functions that already assigned
        # to this class won't be done so again, as it will lead to
        # infinite recursion loop.
        # TODO: Maybe functions that are already assigned should be
        #       converted to special function types..?
        if getattr(_value, "__pygim_parent__", None) is cls:
            return super().__setattr__(_name, _value)
        MutableFuncObject(_value).assign_to_class(cls, _name)

    def __setattr__(cls, _name, _value):
        cls.__do_setattr(_value, _name)


class GimObject(metaclass=GimObjectMeta):
    '''
    '''
    __slots__ = ()

    def __repr__(self):
        return f"{self.__class__.__name__}(0x{id(self)})"


gim_type = GimObjectMeta
gim_object = GimObject
