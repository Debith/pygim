"""

"""

from typing import TypeVar, Mapping
from collections import UserDict, defaultdict as ddict
from .gimdict import GimDict
from .gimlist import GimList


__all__ = ['GimDict', 'nested_walk', 'GimList']


class FilterWalker:
    def __init__(self, filter_func, key_transformer, value_transformer):
        value_transformer = value_transformer or (lambda val: val)
        self._dispatch = ddict(lambda: value_transformer, {
            dict: self._walk_dict,
            list: self._walk_list,
            GimDict: self._walk_dict,
        })
        self._filter_func = filter_func
        self._key_transformer = key_transformer or (lambda key: key)

    def _walk_dict(self, mapping):
        return mapping.__class__(
            {self._key_transformer(k): self._dispatch[type(v)](v) for k, v in mapping.items() if self._filter_func(v)}
        )

    def _walk_list(self, array):
        return [self._dispatch[type(i)](i) for i in array if self._filter_func(i)]

    def __call__(self, obj):
        return self._dispatch[type(obj)](obj)


class KeyWalker:
    def __init__(self, filter_func, key_transformer, value_transformer):
        value_transformer = value_transformer or (lambda val: val)
        self._dispatch = ddict(lambda: value_transformer, {
            dict: self._walk_dict,
            list: self._walk_list,
            GimDict: self._walk_dict,
        })
        self._filter_func = filter_func
        self._key_transformer = key_transformer or (lambda key: key)

    def _walk_dict(self, mapping):
        return GimDict(
            {self._key_transformer(k): self._dispatch[type(v)](v) for k, v in mapping.items() if self._filter_func(v)}
        )

    def _walk_list(self, array):
        return GimList([self._dispatch[type(i)](i) for i in array if self._filter_func(i)])

    def __call__(self, obj):
        return self._dispatch[type(obj)](obj)


def nested_walk(obj, *, filter_func, key_transformer=None, value_transformer=None):
    return FilterWalker(filter_func, key_transformer, value_transformer)(obj)