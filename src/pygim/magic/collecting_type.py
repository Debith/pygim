"""
This module implements class that collects all registered types.
"""

from typing import Text, Any


class CollectingTypeMeta(type):
    def __new__(mcls, name: Text, bases=(), attrs=None):
        return super().__new__(mcls, name, bases, attrs)



class CollectingType(metaclass=CollectingTypeMeta):
    SUBCLASSES = []

    def __init_subclass__(cls, **kwargs) -> None:
        cls.SUBCLASSES.append(cls)
        super().__init_subclass__(**kwargs)