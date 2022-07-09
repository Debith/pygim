"""
More types to support type annotation.
"""

from abc import abstractmethod
import typing as t
from typing import *

__all__ = t.__all__ + ['SupportsStr']


@runtime_checkable
class SupportsStr(Protocol):
    """An ABC with one abstract method __str__."""
    __slots__ = ()

    @abstractmethod
    def __str__(self) -> str:
        pass

# TODO: Fix this
# Nested iterable indicates that
NestedIterable = t.Iterable