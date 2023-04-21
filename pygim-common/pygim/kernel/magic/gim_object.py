# -*- coding: utf-8 -*-
'''
This module implmements class GimObject.
'''
import inspect

from .patch import transfer_ownership
import numpy

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
        return super().__new__(mcls, name, bases, namespace)

    def __call__(self, *args, **kwargs):
        if self is GimObject:
            raise NotImplementedError()
        return super().__call__(*args, **kwargs)

    def __lshift__(cls, other):
        transfer_ownership(cls, other)
        return cls


class GimObject(metaclass=GimObjectMeta):
    '''
    '''
    __slots__ = ()

    def __repr__(self):
        return f"{self.__class__.__name__}(0x{id(self)})"


gim_type = GimObjectMeta
gim_object = GimObject
