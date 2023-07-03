# -*- coding: utf-8 -*-
'''
Internal package for complaining functions.
'''

import inspect
import types

from .._iterlib import flatten

__all__ = ('TraitFunctions', 'has_instances', 'is_subset')

TraitFunctions = (types.FunctionType, types.MethodType)


def has_instances(iterable, types, *, how=all):
    return how(isinstance(it, types) for it in iterable)


def is_subset(iterable, other):
    return set(iterable).issubset(other)


def class_names(*classes):
    for cls in flatten(classes):
        if inspect.isclass(cls):
            yield cls.__name__
        else:
            yield cls.__class__.__name__
