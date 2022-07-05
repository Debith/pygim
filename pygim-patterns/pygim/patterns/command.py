# -*- coding: utf-8 -*-
"""
Command Pattern.
"""

from typing import Iterable, Callable
from dataclasses import dataclass
import abc

__all__ = ['Command']


class CommandMeta(type):
    """ Factory meta-class for Commands. """

    def __call__(self, *args, **kwargs):
        pass


class Command(metaclass=CommandMeta):
    """ Command pattern.

    Args:
        abc (_type_): _description_
    """
    def __init__(self, callable: Callable = None):
        self._callable = callable

    def __call__(self, *args, **kwargs):
        return self._callable(*args, **kwargs)
