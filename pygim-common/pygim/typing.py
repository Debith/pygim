# -*- coding: utf-8 -*-
"""
More types to support type annotation.
"""

from abc import abstractmethod
from pathlib import Path
import typing as t
import typing_extensions as te
from typing_extensions import *
from typing import *

__all__ = t.__all__ + te.__all__ + ['SupportsStr', 'NestedIterable', 'PathLike']


@runtime_checkable
class SupportsStr(Protocol):
    """An ABC with one abstract method __str__."""
    __slots__ = ()

    @abstractmethod
    def __str__(self) -> str:
        pass

# Object type that can used to turn into path.
PathLike = t.Union[t.Text, Path]
MaybePathLike = t.Optional[PathLike]
PathLikes = t.Iterable[PathLike]
MaybePathLikes = t.Optional[PathLikes]

# TODO: Fix this
# Nested iterable indicates that iterable can contain other iterable(s).
NestedIterable = t.Iterable