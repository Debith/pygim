"""Type stubs for pygim.code.

Python source parsed in C++: a corpus of syntax trees, and views to read them.
``parse(root)`` walks, reads and parses a whole tree in one run with the GIL
released; ``parse_source(source)`` parses one text. A ``Tree`` and its ``Node``
objects are views of what the run made: no Python object exists per node until
one is asked for. Kinds, fields and positions are Python 3.14's ``ast``'s.
"""

import os
from collections.abc import Iterator
from typing import TypedDict

from pygim.pathlike import PathSet, path as pygim_path   # Tree has a property named path

StrPath = str | bytes | os.PathLike[str] | os.PathLike[bytes]   # what pygim's paths take


class CorpusStats(TypedDict):
    files: int
    failed: int
    bytes: int
    nodes: int
    names: int
    workers: int
    seconds: float


def parse(root: StrPath, pattern: str = "**/*.py", *, threads: int | None = None) -> Corpus:
    """Every file under root matching pattern, parsed as Python 3.14: one run in C++ that walks,
    reads and parses with the GIL released. threads: how many work at once; None, one per
    core. Ctrl-C stops the run and raises KeyboardInterrupt."""


def parse_source(source: str | bytes, path: StrPath = "<string>") -> Tree:
    """One source (str, or bytes read from a file) parsed as Python 3.14: its Tree, or SyntaxError.
    path: the file it came from (str or any path), named by the tree and its SyntaxError."""


class Corpus:
    """Parsed files, sorted by path, sharing one table of names."""

    def __len__(self) -> int:
        """How many files, parsed or not."""
    def __getitem__(self, file: int | StrPath) -> Tree:
        """A file's tree, by index or path; a file that is not Python raises its SyntaxError."""
    def __iter__(self) -> Iterator[Tree]:
        """The trees of the files that parsed, in path order."""
    @property
    def files(self) -> PathSet:
        """Every file, parsed or not, as a PathSet in path order (corpus[i] is files[i]'s tree)."""
    @property
    def errors(self) -> list[SyntaxError]:
        """Each file that is not Python, in path order, as the SyntaxError corpus[path] raises for it."""
    def stats(self) -> CorpusStats:
        """What the run did: files, failed, bytes, nodes, names, workers, seconds."""


class Tree:
    """One parsed file. A view; it keeps its corpus alive."""

    @property
    def path(self) -> pygim_path:
        """The file, as a pygim path; parse_source()'s path argument for one source."""
    @property
    def root(self) -> Node:
        """The Module node."""
    def __len__(self) -> int:
        """How many rows the tree has, node() reads: its nodes, and the field values kept
        as rows of their own ('identifier' in a list of names, 'cmpop', 'absent' for a missing value)."""
    def node(self, index: int) -> Node:
        """The row at an index (preorder: source order), from 0 to len(tree)."""
    def find(self, kind: str) -> list[Node]:
        """Every node of a kind (as ast names it: 'Name'), in source order; ValueError for a name no kind has."""
    def kind_counts(self) -> dict[str, int]:
        """How many nodes of each kind."""
    def dump(self, *, include_attributes: bool = False) -> str:
        """The tree as ast.dump(ast.parse(source)) writes it, include_attributes as there."""


class Node:
    """One node of a syntax tree: its kind, its fields, its position. A view; it keeps its corpus alive."""

    @property
    def kind(self) -> str:
        """The node's kind, as ast names it: 'FunctionDef', 'Name', ..."""
    @property
    def index(self) -> int:
        """The node's place in its tree, in source order (preorder)."""
    @property
    def parent(self) -> Node | None:
        """The node this one is a field of; None for the module."""
    @property
    def children(self) -> list[Node]:
        """The nodes under this one, in source order."""
    @property
    def fields(self) -> list[str]:
        """The kind's fields, as ast's _fields orders them."""
    def field(self, name: str) -> object:
        """A field's value as ast holds it: a Node, a list, a str, a number, or None."""
    @property
    def lineno(self) -> int | None:
        """The line the node starts on, from 1; None for a kind ast gives no position (Module, arguments)."""
    @property
    def col_offset(self) -> int | None:
        """Bytes from the start of the line, as ast counts them; None as lineno."""
    @property
    def end_lineno(self) -> int | None:
        """The line the node ends on; None as lineno."""
    @property
    def end_col_offset(self) -> int | None:
        """Bytes from the start of the end line to the node's end; None as lineno."""
    @property
    def source(self) -> str:
        """The source text the node spans."""
    def dump(self, *, include_attributes: bool = False) -> str:
        """The node as ast.dump writes it."""
    def __eq__(self, other: object) -> bool:
        """The same node of the same tree."""
    def __hash__(self) -> int: ...
