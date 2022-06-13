# -*- coding: utf-8 -*-
"""

"""

from dataclasses import dataclass
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


"""

# TODO: make cached
@dataclass
class GimDictKey:
    _key: list

    def __post_init__(self):
        if isinstance(self._key, (set, tuple)):
            self._key = list(self._key)

        elif isinstance(self._key, str):
            self._key = self._key.split('.')

        elif not isinstance(self._key, list):
            self._key = [str(self._key)]

    def __repr__(self):
        return f"<Key:{str(self._key)}>"

    def __str__(self):
        return ".".join(self._key)

    def __add__(self, other):
        assert isinstance(other, (GimDictKey, str))
        return self.__class__(self._key + [str(other)])
"""


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
    def __and__(self, *filters):
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