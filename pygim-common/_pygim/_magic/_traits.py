# -*- coding: utf-8 -*-
'''

'''

from types import FunctionType
from dataclasses import dataclass, field
import inspect

from ._patch import MutableFuncObject
from .._utils import flatten, has_instances


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
        assert callable(func)
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


def combine(*classes, class_name="NewType", bases=()):
    from ._gim_object import gim_type  # TODO: relocate
    NewType = gim_type(class_name, bases)
    orig_funcs = set(dir(NewType))

    funcs = []
    for _class in classes:
        cls_funcs = set(dir(_class))
        new_funcs = cls_funcs - orig_funcs

        for func in new_funcs:
            funcs.append(_class.__dict__[func])

    NewType << funcs
    return NewType