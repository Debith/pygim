"""Type stubs for pygim.similarity.

Deterministic structural similarity of functions and classes. An ``Index``
holds the units of ONE language and compares every one with every other of its
kind; languages never meet. Results are ``pyarrow.Table``s, or for ``stream_pairs()``
``pyarrow.RecordBatchReader``s, built in C++. The
language set is OPEN (one C++ header per language, discovered by the build):
``LANGUAGES`` lists what this build has.
"""

import os
from collections.abc import Iterable, Sequence
from typing import Literal, TypedDict

import pyarrow

Kind = Literal["function", "class"]
StrPath = str | bytes | os.PathLike[str] | os.PathLike[bytes]   # what pygim's paths take: utf8_fspath
PairColumn = Literal["a", "b", "a_qualname", "a_origin", "a_line", "b_qualname", "b_origin", "b_line",
                     "score", "shared", "total", "exact"]


class LanguageRecord(TypedDict):
    doc: str
    exts: tuple[str, ...]


class IndexStats(TypedDict):
    units: int
    functions: int
    classes: int
    subtrees: int
    bytes: int


LANGUAGES: dict[str, LanguageRecord]
"""Every language of this build: ``{"cpp": {...}, "python": {...}}``. No
extension belongs to two languages."""


def language_of(path: StrPath) -> str | None:
    """The language owning `path`'s extension ("python", "cpp"), or None."""


class Index:
    """The units of one language and their all-pairs comparison.

    Index(language) -- language is a key of LANGUAGES ("python", "cpp")."""

    def __init__(self, language: str) -> None:
        """An empty index of `language` (a key of LANGUAGES); ValueError naming the known languages otherwise."""
    @property
    def language(self) -> str:
        """The language this index holds."""
    def add(self, source: str, origin: str = "<string>", module: str | None = None) -> None:
        """Parse `source` (str) and add its units, recorded as coming from `origin`.

        `module` is the source's dotted module name ("pkg.sub.m"): it qualifies the
        module-level names and resolves relative imports, so the same function
        reached through different imports compares equal; relative imports start from
        its parent, or from the module itself when `origin` names an __init__ file.
        Without it, names resolve as far as the source itself says. A source the
        frontend cannot read raises ValueError "<origin>:<line>: ..."."""
    def add_file(self, path: StrPath, module: str | None = None) -> None:
        """Read the file at `path` and add its units.

        The module name is `module`, or else derived the way the import system would:
        the file's stem under every parent directory holding an __init__.py. ValueError
        when the file's extension is not this index's language."""
    def add_files(self, files: Iterable[StrPath], *, threads: int | None = None) -> list[tuple[str, str]]:
        """Read, parse and fingerprint many files at once, then add their units in the order given.

        The work runs in C++ on worker threads with the GIL released, and Ctrl-C stops it (KeyboardInterrupt);
        the units' ids are what add_file() in a loop would give them. ValueError, before anything is read,
        when a file's extension is not this index's language. Returns (path, message) for each file left
        out: not a source the language's frontend can read, refused by the OS, or no regular file.
        threads: how many work at once; None grows them while more are worth it."""
    def units(self, kind: Kind | None = None) -> pyarrow.Table:
        """The units in insertion order, optionally only kind="function" or "class",
        as a pyarrow.Table:

          id u32 | kind dictionary | qualname str | origin dictionary | line u32 |
          owner u32 (a method's class id; null otherwise) | nodes u32 | weight u64"""
    def pairs(self, kind: Kind = "function", threshold: float = 0.0, min_size: int = 0, limit: int = 0,
              batch_size: int = 1 << 20, columns: Sequence[PairColumn] | None = None) -> pyarrow.Table:
        """Every pair of units of `kind` scoring at least `threshold`, RANKED best first,
        as a pyarrow.Table.

        Units of fewer than `min_size` syntax-tree nodes (a class counts its methods')
        are left out: trivial accessors are identical without being duplicates. With
        `limit`, only the best `limit` pairs are kept, and the sweep skips every
        comparison that could not make the cut. The table is made of batches of
        `batch_size` rows.

        Columns, one row per pair:

          a, b u32 (unit ids; join units().id) | a_qualname, b_qualname dictionary |
          a_origin, b_origin dictionary | a_line, b_line u32 |
          score f64 in [0, 1] | shared, total u64 (score == 2 * shared / total,
          exactly) | exact bool (identical up to local names)

        `columns` selects and orders the columns (default: all). ["a", "b", "score"]
        is 16 bytes a row instead of 56: names, files and lines join from units() by
        id. Ranking holds every pair in memory; for results too large for that, use
        stream_pairs(). Sweep, sort and table building run on every core with the GIL
        released."""
    def stream_pairs(self, kind: Kind = "function", threshold: float = 0.0, min_size: int = 0,
                     batch_size: int = 1 << 20, columns: Sequence[PairColumn] | None = None) -> pyarrow.RecordBatchReader:
        """The pairs of pairs() (without a limit), UNRANKED, as a pyarrow.RecordBatchReader.

        Batches of exactly `batch_size` rows (the last shorter) are swept on every
        core while the reader is consumed, in a fixed order: the same batches every
        run, for any thread count. Memory stays bounded whatever the result size — by
        `batch_size` and the cores: a few batches being written at threshold 0, two
        blocks a worker at a threshold — so 100M+ pairs can be written to Parquet,
        aggregated or filtered as they arrive.
        Same columns, and `columns` selection, as pairs(). Adding to the index while a
        stream is open fails the stream (ArrowInvalid)."""
    def stats(self) -> IndexStats:
        """Counts and the corpus's exact bytes: units, functions, classes, subtrees, bytes (the units, their fingerprints and names;
        not the columns and orders a query keeps beside them)."""
    def __len__(self) -> int:
        """How many units the index holds: functions, methods and classes."""
    def __repr__(self) -> str:
        """similarity.Index('python', units=9)."""
