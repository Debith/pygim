"""
This module implements null object pattern that is a singleton.
"""

from typing import Text

from .cached_type import CachedType


class NullClassFactoryMeta(CachedType):
    def __new__(mcls, name: Text, bases=(), attrs=None):
        return super().__new__(mcls, name, bases, attrs or {}, cache_class=True, cache_instance=True)

    @classmethod
    def make_class(mcls, name, bases, attrs):
        return super().make_class(name, bases, attrs)


class Test:
    pass        