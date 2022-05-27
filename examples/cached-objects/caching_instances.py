"""
This example demonstrates caching object instances.

Effectively, instance caching works similarly to lru_cache for functions
or cached_property for properties, only in this case, it caches full
objects.
"""

import pyximport; pyximport.install()

from pygim import decorators

__type_cache__ = {}

def _cached(func):
    def _decorated(self, *args, **kwargs):
        kwargs_key = tuple(kwargs.items())
        try:
            return __type_cache__[self][args, kwargs_key]
        except KeyError:
            instance = func(self, *args, **kwargs)
            __type_cache__[self][args, kwargs_key] = instance
            return instance

    return _decorated


@decorators.cached
class Currency:
    def __new__(cls, value, prefix="", postfix="", **kwargs):
        return super().__new__(cls)

    def __init__(self, value, prefix="", postfix=""):
        self._value = value
        self._prefix = prefix
        self._postfix = postfix

    def __init_subclass__(cls, **kwargs) -> None:
        print(kwargs)

    def __int__(self):
        return self._value

    def __str__(self):
        return f'{self._prefix}{self._value}{self._postfix}'



wallet1 = Currency(101, prefix='$')
wallet2 = Currency(101, prefix='$')

assert wallet1 is wallet2

print("Done!")