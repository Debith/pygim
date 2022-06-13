""" This module contains main gimmick object with selected set of features. """

__all__ = ["GimObjectMeta", "GimObject"]

from collections import UserDict
from typing import Text, Any
from .collecting_type import CollectingTypeMeta


class MangledDict(UserDict):
    def __init__(self, owner_name):
        self._owner_name = owner_name
        self.data = {}

    def __setitem__(self, key, value):
        self.data[f'_{self._owner_name}__{key}'] = value


class ClassBuilder:
    def __init__(self, mcls, name, bases, attrs, create_class: callable):
        self._mcls = mcls
        self._name = name
        self._bases = bases
        self._mangled_attrs = MangledDict(name)
        self._attrs = attrs
        self._create_class = create_class

    def add_tracking(self):
        self._attrs['subclasses'] = []

        def __init_subclass__(cls, **kwargs):
            cls.subclasses.append(cls)
            super(cls, cls).__init_subclass__(**kwargs)

        self._attrs['__init_subclass__'] = __init_subclass__

    def create(self):
        attrs = {**self._attrs, **self._mangled_attrs}
        return self._create_class(self._mcls, self._name, self._bases, attrs)


class GimObjectMeta(type):
    def __new__(mcls, name: Text, bases=(), attrs=None, *,
        instance_cached=False,
        interface=False,
        abstract=False,
        track_subclasses=False,
        ):
        builder = ClassBuilder(mcls, name, bases, attrs, super().__new__)

        if track_subclasses:
            builder.add_tracking()

        return builder.create()

    @classmethod
    def make_class(mcls, name, bases, attrs):
        return super().__new__(mcls, name, bases, attrs)


class GimObject(metaclass=GimObjectMeta):
    """ Main object to be used for everything cool. """
