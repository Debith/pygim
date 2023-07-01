# -*- coding: utf-8 -*-
'''

'''

from types import FunctionType
from dataclasses import dataclass, field
import inspect

from ._dispatcher import _Dispatcher
from ._patch import MutableFuncObject
from .._utils import flatten
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