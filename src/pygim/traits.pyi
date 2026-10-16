"""Traits: behaviour written once and composed into classes.

The design is docs/design/traits.md.
"""
from types import CodeType

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
