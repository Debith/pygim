# -*- coding: utf-8 -*-
"""

"""

from dataclasses import dataclass
from pprint import pprint, pformat
from re import I
from typing import TypeVar, Mapping
from collections import UserDict

from enum import Enum

from ..utils.iterable import flatten
from .string_list import StringList

class Target(Enum):
    KEY = "key"
    VALUE = "value"


KT = TypeVar("KT")
VT = TypeVar("VT")



GimDictKey = StringList('GimDictKey', sep='.')


class GimDict(UserDict):
    """
    Super handy dictionary class.

    It works as a normal dictionary:
    >>> gim_dict = GimDict({"hello": "dict"})
    >>> gim_dict["hello"]
    'dict'

    It is possible to refer values that are deep inside.
    >>> gim_dict = GimDict({"nested": {"hello": "nested dict"}})
    >>> gim_dict["nested.hello"]
    'nested dict'
    """
    @dataclass
    class _ValueFilter:
        _filters: list

        def __post_init__(self):
            self._filters = list(self._filters)

        def __iter__(self):
            yield from self._filters

        def __repr__(self):
            return repr(list(self._filters))

        def __contains__(self, other):
            for f in self._filters:
                if callable(f) and f(other):
                    return True
                else:
                    return other == f

    @dataclass
    class _KeyFilter:
        _filters: list

        def __post_init__(self):
            self._filters = list(self._filters)

        def __iter__(self):
            yield from self._filters

        def __repr__(self):
            return repr(list(self._filters))

        def __contains__(self, other):
            for f in self._filters:
                if callable(f):
                    return f(other)
                else:
                    return other == f

    def __repr__(self):
        return pformat(self.data)

    def key_filter(self, *filters):
        return self._KeyFilter(flatten(filters))

    def value_filter(self, *filters):
        return self._ValueFilter(flatten(filters))

    def __and__(self, filters):
        if isinstance(filters, self._ValueFilter):
            return self.__class__({k: v for k, v in self.items() if v in filters})
        elif isinstance(filters, self._KeyFilter):
            return self.__class__({k: self[k] for k in self if k in filters})
        else:
            return self.__class__({f: self[f] for f in flatten(filters) if f in self})

    def _nested_access(self, key):
        value = self.data
        for part in key.split('.'):
            value = value[part]
        return value

    def __getitem__(self, key: KT) -> VT:
        try:
            value = self._nested_access(key)
        except (KeyError, TypeError):
            raise KeyError(f"Given key '{key}' was not found!") from None
        if isinstance(value, Mapping):
            return self.__class__(value)
        return value

    def accept(self, visitor, *, parent_key=''):
        # TODO: reconsider visitor
        for key, value in self.items():
            key = (parent_key + f'.{key}').lstrip('.')
            visitor(key, value)
            try:
                value.accept(visitor, parent_key=key)
            except AttributeError:
                pass

    def flattened_keys(self):
        new_keys = []

        def visitor(key, _):
            new_keys.append(key)

        self.accept(visitor)
        return new_keys



if __name__ == "__main__":
    import doctest
    doctest.testmod()