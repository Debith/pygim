# -*- coding: utf-8 -*-
'''
Internal package for complaining functions.
'''

from dataclasses import dataclass
import inspect
import types

from .._iterlib import flatten

__all__ = ('TraitFunctions', 'has_instances', 'is_subset', 'has_uniform_type')

# This is an alias for the types that are used for traits. This is used
# for the `has_instances` function. For example:
# >>> from pygim._utils import has_instances, TraitFunctions
# >>> has_instances([1, 2, 3], TraitFunctions)
# False
TraitFunctions = (types.FunctionType, types.MethodType)


def has_instances(iterable, types, *, how=all):
    return how(isinstance(it, types) for it in iterable)


def has_uniform_type(iterable):
    return len(set(map(type, iterable))) == 1


@dataclass
class SubSetResult:
    subset: set
    superset: set

    def __post_init__(self):
        self.subset = set(self.subset)
        self.superset = set(self.superset)

    def __bool__(self):
        return set(self.subset).issubset(self.superset)

    def missing(self):
        return sorted(self.subset - self.superset)

    def extra(self):
        return sorted(self.superset - self.subset)


def is_subset(subset, superset):
    return SubSetResult(subset, superset)


def class_names(*classes):
    for cls in flatten(classes):
        if inspect.isclass(cls):
            yield cls.__name__
        else:
            yield cls.__class__.__name__
