"""Traits: behaviour written once and composed into classes.

The design is docs/design/traits.md.
"""
from collections.abc import Iterator
from types import CodeType
from typing import TypeVar

_T = TypeVar("_T", bound=type)

def compose(target: _T, *traits: type, override: bool = False) -> _T:
    """Copy every member of every trait into ``target``, rewritten to belong there, and return it.

    A trait is a class whose functions, properties, classmethods and staticmethods are written to
    be copied; nothing inherits from it. Each copy reads ``target``'s private names, carries
    ``target``'s qualified name, and its zero-argument ``super()`` resolves in ``target``. Refused,
    writing nothing: a trait that is not a class or holds any other member (``TypeError``); a name
    ``target`` already defines, or two traits both define (``ValueError``). ``override=True``
    replaces instead, the later trait winning.
    """

def traits_of(cls: type) -> TraitRecord:
    """The record of what was composed into ``cls`` itself; empty when nothing was."""

class Provenance:
    """Where a composed member came from."""

    @property
    def trait(self) -> str:
        """The trait's module and qualified name."""
    @property
    def defined(self) -> str:
        """``file:line`` where the trait defined the member."""
    @property
    def composed(self) -> str:
        """``file:line`` of the ``compose()`` call."""

class TraitRecord:
    """The members composed into one class, by name, in name order."""

    def __getitem__(self, name: str) -> Provenance: ...
    def __contains__(self, name: object) -> bool: ...
    def __len__(self) -> int: ...
    def __iter__(self) -> Iterator[str]: ...
    def keys(self) -> list[str]:
        """The composed members' names, in name order."""

class CodeBuilder:
    """A code object rebuilt so that it belongs to another class.

    Inside ``class Trait:``, Python compiles ``self.__secret`` as ``self._Trait__secret``. Code moved
    into another class must read that class's spelling instead; ``reown`` records the move and
    ``build`` makes a new code object with it applied — to the code and every code object nested in
    it. The original code object is never changed.
    """

    def __init__(self, code: CodeType) -> None:
        """Start from ``code``, a code object; it is never changed."""

    def reown(self, source: type, target: type) -> CodeBuilder:
        """Move the code from class ``source`` to class ``target``: private and qualified names.
        Returns this builder, so steps chain."""

    def build(self) -> CodeType:
        """A new code object with every step applied, in order, to the code and every code object
        nested in it."""
