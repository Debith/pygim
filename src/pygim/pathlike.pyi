"""Type stubs for pygim.pathlike.

The compiled extension is untyped at runtime; this stub documents the stable
public contract of ``pygim.path``: one class, a handle on a row of a path table.

The engine set is OPEN (one C++ header per engine, discovered by the build), so
everything engine-dependent — the ``Engine`` selector literal, the
``<name>path`` typed classes and ``ENGINES`` — lives in the generated block at
the end, rendered from the built module by ``pygim stubs`` and kept current by
a test.
"""

import os
from typing import Any, Iterable, Iterator, Literal, NamedTuple, overload

PathLike = str | bytes | os.PathLike[str] | path


class path(os.PathLike[str]):
    """A filesystem path that reads and decodes itself: a handle (table, row) on a
    PathStore's table, where every distinct component is stored once. ``path("x.yaml")``
    is a ``yamlpath`` (the typed class of the engine its extension resolves to);
    ``engine=`` pins one; ``store=`` chooses the table this path and everything derived
    from it live in (default: ``default_store()``). Equal paths compare and hash alike
    whatever their spelling; two handles are not promised to be the same object."""
    def __init__(self, path: str | bytes | os.PathLike[str], engine: Engine | None = None,
                 *, store: PathStore | None = None) -> None: ...

    # -- decoding / encoding ------------------------------------------------
    def read(self, engine: Engine | None = None, key_cache: int = 256) -> Any:
        """Decode the file to native Python objects (GIL released during I/O
        and parsing). ``key_cache`` bounds key interning: 0 off, -1 unbounded."""
    def write(self, obj: Any, engine: Engine | None = None, *, atomic: bool = False, append: bool = False,
              parents: bool = False) -> None:
        """Serialise ``obj`` with the resolved engine. Strings that would read
        back typed are quoted, so write/read round-trips. ``atomic=True`` writes a
        hidden temporary sibling, forces it to the disk and renames it over the
        file: a reader sees the old contents or the new, never part of either,
        and after a power cut one of them is there whole. The file is then a new
        one (a second hard link keeps the old contents; a symlink is written
        through). ``append=True`` adds obj's documents to the file's end, the
        file made when it is not there, for an engine whose documents are lines
        (jsonl); the others refuse it, and so does ``atomic``. ``parents=True``
        makes the folders the file needs first. Format constraints are the
        engine's own (see each ``<name>path`` docstring)."""
    def read_bytes(self) -> bytes: ...
    def write_bytes(self, data: bytes, *, atomic: bool = False) -> None:
        """Replace the file's contents with *data*, byte for byte; ``atomic=True``
        as for :meth:`write`."""
    def mkdir(self, parents: bool = False, exist_ok: bool = False) -> None:
        """Create this directory, with pathlib's parents/exist_ok semantics."""
    def unlink(self, missing_ok: bool = False) -> None:
        """Remove this file or symbolic link, with pathlib's missing_ok semantics."""

    @property
    def engine(self) -> str | None:
        """The library label read()/write() will use ('rapidyaml', 'simdjson', ...):
        the constructor pin, else the extension — or None when neither resolves."""
    @property
    def store(self) -> PathStore:
        """The store (table) this path lives in; derived paths share it."""

    # -- os.PathLike / identity ---------------------------------------------
    def __fspath__(self) -> str:
        """The native path text in pathlib's normalised spelling ('a//b/' -> 'a/b')."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    # pathlib's order (str(p).split(sep), part by part); another type is a TypeError.
    def __lt__(self, other: path) -> bool: ...
    def __le__(self, other: path) -> bool: ...
    def __gt__(self, other: path) -> bool: ...
    def __ge__(self, other: path) -> bool: ...
    # pickle and copy, as pathlib's and Decimal's; a user's subclass is copied by its recipe.
    def __reduce__(self) -> tuple[type[path], tuple[str] | tuple[str, str]]:
        """pickle's recipe: the class, the text, and the engine when one is pinned. It loads into the default store."""
    def __copy__(self) -> path:
        """The path itself: a path is a value."""
    def __deepcopy__(self, memo: object) -> path:
        """The path itself: a path is a value."""

    # -- name components (pathlib parity), read from the table ---------------
    @property
    def uri(self) -> str:
        """An absolute path as an RFC 3986 file URI (percent-encoded: 'file:///a%20b',
        'file://host/share/x'); a relative path keeps the 'file://<path>' spelling."""
    @property
    def name(self) -> str: ...
    @property
    def stem(self) -> str: ...
    @property
    def suffix(self) -> str: ...
    @property
    def suffixes(self) -> list[str]: ...
    @property
    def parts(self) -> list[str]: ...
    @property
    def depth(self) -> int:
        """Named components below the anchor."""

    # -- composition / derived paths (results share the table and inherit the pin) --
    def __truediv__(self, other: str | bytes | os.PathLike[str]) -> path: ...
    def __rtruediv__(self, other: str | bytes | os.PathLike[str]) -> path: ...
    def joinpath(self, *parts: str | bytes | os.PathLike[str]) -> path: ...
    @property
    def parent(self) -> path: ...
    @property
    def parents(self) -> list[path]: ...
    def with_suffix(self, suffix: str) -> path: ...
    def with_name(self, name: str) -> path: ...
    def with_stem(self, stem: str) -> path: ...
    def relative_to(self, other: str | bytes | os.PathLike[str], /) -> path: ...
    def is_relative_to(self, other: str | bytes | os.PathLike[str], /) -> bool: ...
    def absolute(self) -> path: ...
    def resolve(self, strict: bool = ...) -> path:
        """pathlib's resolve(): an absolute copy, its symlinks and '..' resolved; strict, a missing part is FileNotFoundError."""
    def read_text(self, encoding: str = ..., errors: str | None = ..., newline: str | None = ...) -> str:
        """pathlib's read_text(): the file decoded (UTF-8 unless encoding says, whatever the locale, as Python 3.15 does), every line ending read as \\n unless newline is given."""
    def write_text(self, data: str, encoding: str = ..., errors: str | None = ..., newline: str | None = ...,
                   *, atomic: bool = ...) -> int:
        """pathlib's write_text(): data encoded (UTF-8 unless encoding says), each \\n written as os.linesep unless newline is given; the characters written. atomic=True as for write()."""
    def expanduser(self) -> path:
        """pathlib's expanduser(): a leading ~ or ~user replaced by that user's home; RuntimeError when no home is known."""

    # -- filesystem status --------------------------------------------------
    def is_absolute(self) -> bool: ...
    def exists(self) -> bool: ...
    def is_file(self) -> bool: ...
    def is_dir(self) -> bool: ...
    def is_symlink(self) -> bool: ...
    def size(self) -> int: ...

    # -- directory traversal ------------------------------------------------
    def iterdir(self) -> list[path]: ...
    def glob(self, pattern: str, *, case_sensitive: bool | None = None) -> list[path]:
        """What pathlib's glob finds (``recurse_symlinks=False``): ``*``, ``?`` and ``[seq]`` within
        a name (dot names too), ``**`` across directories, yielding the one it starts in and never
        going through a link; a trailing ``/`` keeps directories; ``.`` is dropped, ``..`` a step up.
        case_sensitive: None as the system (any case on Windows), False any case. Sorted and
        deduplicated; results inherit the engine pin."""
    def rglob(self, pattern: str, *, case_sensitive: bool | None = None) -> list[path]: ...
    def pathset(self, pattern: str = "*", *, case_sensitive: bool | None = None,
                kind: Literal["file", "dir"] | None = None, skip: Iterable[str] = ()) -> PathSet:
        """What glob(pattern) finds, as a PathSet over this path's table, for set algebra and Filter queries.
        kind: 'file' keeps what is_file() says is a file, 'dir' what is_dir() says is a directory (a link
        followed; the walk's listing tells, a link alone is stat'ed); None keeps every match.
        skip: names of folders to pass over (globs, as the pattern's case_sensitive says): a folder so
        named, or a link to one, is never entered or matched, nor is anything beneath it; a file so
        named is kept. `path(root).pathset('**/*.py', kind='file', skip=['.git', 'build'])`."""


class EngineInfo(NamedTuple):
    """One entry of ``ENGINES``: the registry as data."""
    name: str
    label: str
    extensions: tuple[str, ...]
    aliases: tuple[str, ...]
    doc: str

class PathSet:
    """Many paths as ONE table: every distinct component stored once, every
    path a (parent, name) row. Iterating yields path objects over the table; scan() reuses one.
    Filters and |, &, - return sets sharing the table; to_list() renders the members."""
    def __init__(self, paths: PathLike | Iterable[PathLike] | None = None,
                 *, store: PathStore | None = None) -> None: ...
    @staticmethod
    def cwd() -> PathSet: ...
    def __bool__(self) -> bool: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    def clone(self) -> PathSet: ...
    def add(self, path: PathLike) -> None: ...
    def extend(self, paths: Iterable[PathLike]) -> None: ...
    def reserve(self, n: int) -> None: ...
    def __len__(self) -> int: ...
    def __contains__(self, path: object) -> bool: ...
    def __iter__(self) -> Iterator[path]: ...
    def scan(self) -> Iterator[path]:
        """Iterate reusing ONE plain path object (its row advances): no allocation per element."""
    def __getitem__(self, i: int) -> path: ...
    def filter_suffix(self, suffix: str) -> PathSet: ...
    def filter_name(self, glob: str) -> PathSet: ...
    def filter_absolute(self) -> PathSet: ...
    def filter(self, filter: Filter) -> PathSet: ...
    @overload
    def __or__(self, other: PathSet) -> PathSet: ...
    @overload
    def __or__(self, other: Filter) -> Query: ...
    @overload
    def __and__(self, other: PathSet) -> PathSet: ...
    @overload
    def __and__(self, other: Filter) -> Query: ...
    def __sub__(self, other: PathSet | PathLike | Iterable[PathLike]) -> PathSet: ...
    def __add__(self, other: PathSet | PathLike | Iterable[PathLike]) -> PathSet: ...
    def read_all_files(self, *, threads: int | None = None) -> list[str]: ...
    def read_each(self, *, threads: int | None = None, ahead_bytes: int = 64 * 2**20) -> Iterator[FileContent]: ...
    def sizes(self) -> list[int]:
        """Each member's size in bytes, in member order, as path.size() gives it (a link followed); the
        stats run with the GIL released. A member that is not there, or is not a file, raises the
        OSError path.size() raises, naming it."""
    def count_union(self, other: PathSet) -> int: ...
    def count_intersection(self, other: PathSet) -> int: ...
    def count_difference(self, other: PathSet) -> int: ...
    def relative_to(self, other: str | bytes | os.PathLike[str], /) -> PathSet: ...
    def to_list(self) -> list[str]: ...
    def stats(self) -> dict[str, int]: ...


class Filter:
    """A predicate over the paths of a PathSet (ext(), name(), absolute()) with & | ~;
    apply with `ps & f` (a lazy Query) or ps.filter(f). Reads the table, never the filesystem."""
    def __and__(self, other: Filter) -> Filter: ...
    def __or__(self, other: Filter) -> Filter: ...
    def __invert__(self) -> Filter: ...


class Query:
    """A lazy filter over a PathSet (`ps & f`): & narrows, | widens, eval() runs it."""
    def __and__(self, other: Filter) -> Query: ...
    def __or__(self, other: Filter) -> Query: ...
    def eval(self) -> PathSet: ...
    def __len__(self) -> int: ...
    def __iter__(self) -> Iterator[path]: ...


def ext(*suffixes: str, case_sensitive: bool = True) -> Filter:
    """Filter: pathlib's final suffix is one of `suffixes` ('.yaml'; ext('.h', '.cpp')), compared
    exactly, or but for the case of ASCII letters when case_sensitive is False."""

def name(glob: str) -> Filter:
    """Filter: the final component matches the glob (`*` and `?`)."""

def absolute() -> Filter:
    """Filter: pathlib's is_absolute()."""

def match_pattern(pattern: str, string: str) -> bool:
    """Whether `string` matches the glob segment `pattern` (`*`, `?` and `[seq]`), as pathlib reads one."""


class FileContent:
    """One member's bytes, read by PathSet.read_each(): held in C++ and shown as a read-only buffer,
    so memoryview(content) copies nothing."""
    @property
    def path(self) -> path: ...
    def __len__(self) -> int: ...
    def __bytes__(self) -> bytes: ...
    def __buffer__(self, flags: int, /) -> memoryview: ...
    def text(self) -> str: ...

class PathStore:
    """A table of paths: every distinct component once, every path a row. path(text, store=s)
    interns into s's table and every path derived from that one stays there — a store is how a
    program scopes a lifetime: pass one, keep it, drop it. Rows are never freed within a table;
    the table is freed with its last handle."""
    def __init__(self) -> None: ...
    def reserve(self, n: int) -> None: ...
    def stats(self) -> dict[str, int]: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...


def default_store() -> PathStore:
    """The module's default PathStore: what path(text) without store= interns into."""


def decode(text: str | bytes, *, engine: Engine, key_cache: int = 256) -> Any:
    """An engine's text (str, or bytes as they are) as native Python objects, as read() makes of a file holding it: engine= names the engine (no file name tells it); key_cache as for read()."""


def encode(obj: Any, *, engine: Engine) -> str:
    """obj as an engine's text, the text write() puts in a file: engine= names the engine."""


# --- generated: engines (regenerate with `pygim stubs`) ---
# Selection accepts FORMAT names, LIBRARY labels and aliases; .engine reports the label.
Engine = Literal[
    "json", "simdjson",
    "jsonl", "simdjson-ndjson", "ndjson",
    "toml", "toml++", "tomlplusplus",
    "yaml", "rapidyaml", "yml",
]

class jsonpath(path):
    """JSON via simdjson (strict, SIMD-accelerated); writes real JSON and rejects non-finite floats. Constructing one pins the engine (simdjson)."""
    def __init__(self, path: str | bytes | os.PathLike[str], *, store: PathStore | None = None) -> None: ...

class jsonlpath(path):
    """JSON Lines (ndjson) via simdjson's document stream: reads as a list, one item per document, and writes one compact document per line: a list's items, or one document. Constructing one pins the engine (simdjson-ndjson)."""
    def __init__(self, path: str | bytes | os.PathLike[str], *, store: PathStore | None = None) -> None: ...

class tomlpath(path):
    """TOML via toml++: documents are tables (mapping root), there is no null, and dates and times are datetime objects (tomllib parity). Constructing one pins the engine (toml++)."""
    def __init__(self, path: str | bytes | os.PathLike[str], *, store: PathStore | None = None) -> None: ...

class yamlpath(path):
    """YAML 1.2 (core schema) via rapidyaml: anchors, aliases and merge keys are resolved; strings that would read back typed are quoted on write. Constructing one pins the engine (rapidyaml)."""
    def __init__(self, path: str | bytes | os.PathLike[str], *, store: PathStore | None = None) -> None: ...

ENGINES: tuple[EngineInfo, ...]
# --- end generated ---
