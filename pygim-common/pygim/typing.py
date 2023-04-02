# -*- coding: utf-8 -*-
"""
More types to support type annotation.
"""

from abc import abstractmethod
from pathlib import Path
import typing as t
import typing_extensions as te
from typing_extensions import TypeAlias, runtime_checkable, Protocol
from typing import Union, Optional, Iterable, Tuple, Dict, Callable, Text, Type, Any

__all__ = [
    "PathLike",
    "MaybePathLike",
    "PathLikes",
    "MaybePathLikes",
    "NestedIterable",
    "AnyClass",
    "AnyArgs",
    "NamespaceDict",
    "AnyKwargs",
    "AnyCallable",
]

__all__ += t.__all__ + te.__all__


@runtime_checkable
class SupportsStr(Protocol):
    """An ABC with one abstract method __str__."""

    __slots__ = ()

    @abstractmethod
    def __str__(self) -> str:
        pass


# Object type that can used to turn into path.
PathLike = Union[Text, Path]
MaybePathLike = Optional[PathLike]
PathLikes = Iterable[PathLike]
MaybePathLikes = Optional[PathLikes]

# TODO: Fix this
# Nested iterable indicates that iterable can contain other iterable(s).
NestedIterable = Iterable

AnyClass: TypeAlias = Type[Any]
AnyArgs: TypeAlias = Tuple[Any, ...]
NamespaceDict = Dict[Text, Any]
AnyKwargs: TypeAlias = NamespaceDict
AnyCallable: TypeAlias = Callable[[Any, Any], Any]
