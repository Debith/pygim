# -*- coding: utf-8 -*-
"""
More types to support type annotation.
"""

import os
from abc import abstractmethod
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

# Intentionally NOT re-exporting typing/typing_extensions __all__;
# only the symbols defined in this module are part of our public surface.


@runtime_checkable
class SupportsStr(Protocol):
    """An ABC with one abstract method __str__."""

    __slots__ = ()

    @abstractmethod
    def __str__(self) -> str:
        pass


# What a path is taken as: text, or anything os.fspath takes (pathlib's, pygim's) — the protocol, never one class.
PathLike = Union[Text, "os.PathLike[str]"]
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
