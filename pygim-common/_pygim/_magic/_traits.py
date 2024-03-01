# -*- coding: utf-8 -*-
'''

'''

from types import FunctionType
from dataclasses import dataclass, field
from functools import update_wrapper
import inspect

from ._dispatcher import _Dispatcher
from ._patch import MutableFuncObject
from .._utils import flatten, mgetattr, has_uniform_type
from .._error_msgs import type_error_msg

dispatch = _Dispatcher

def transfer_ownership(target, *funcs):
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
    assert inspect.isclass(target)

    for func in flatten(funcs):
        assert callable(func), type_error_msg(func, FunctionType)
        func_obj = MutableFuncObject(func)
        func_obj >> target


@dataclass
class Relocator:
    _filters: list = field(default_factory=lambda: [])

    def __call__(self, target, namespace, names):
        if inspect.isclass(namespace):
            namespace = namespace.__dict__
        assert set(names).issubset(namespace)

        for name in names:
            if name in self._filters:
                continue
            setattr(target, name, namespace[name])


@dispatch
def _combine(*args, **kwargs):
    raise TypeError("Unsupported Type")


@_combine.register(FunctionType)
def _combine_func(trait, target):
    target << trait


@_combine.register(type)
def _combine_class(trait, target):
    target_funcs = set(dir(target))
    cls_funcs = set(dir(trait))
    new_funcs = cls_funcs - target_funcs

    for func in new_funcs:
        target << trait.__dict__[func]


def combine(*traits, class_name="NewType", bases=()):
    from ._gimmick import gim_type  # TODO: relocate
    NewType = gim_type(class_name, bases)
    NewType << traits

    return NewType





class MultiClassMeta(type):
    def _fix_name(self, name, prefix, postfix):
        if prefix:
            name = f"{prefix}_{name}"
        if postfix:
            name = f"{name}_{postfix}"
        return name

    def _update_attrs(self, obj, prefix, postfix):
        pass

    def __call__(cls, *args, **kwargs):
        return cls.__new__(cls, *args, **kwargs)


class MultiClass(metaclass=MultiClassMeta):
    """ Wrapper for calling multiple objects at once."""

    def __post_init__(self):
        # ensure that all objects are of the same type, for now.
        # TODO: Add support for different types by collecting all functions found
        #       from all objects and then adding them to the new class.
        assert has_uniform_type(self._objects), "All objects must be of the same type."

        for name in dir(self._objects[0]):
            if name.startswith("_"):
                continue
            # For now, we only support functions.
            if not callable(getattr(self._objects[0], name)):
                continue

            new_callable = MultiCall(name, self._objects)
            setattr(self, self.__make_name(name), new_callable)
            update_wrapper(new_callable, getattr(self._objects[0], name))
