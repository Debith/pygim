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
    """A filesystem path that knows how to read and decode itself: a handle on a row of a path table
    (every distinct component stored once). The decoding engine is chosen from the extension
    (``.json`` -> json (simdjson); ``.jsonl``/``.ndjson`` -> jsonl (simdjson-ndjson); ``.toml``
    -> toml (toml++); ``.yaml``/``.yml`` -> yaml (rapidyaml)), and ``path("x.yaml")`` is a
    ``yamlpath``; pin one with ``engine=`` at construction, or override per call with
    ``read(engine=...)``. ``store=`` chooses the table (a PathStore) the path and everything
    derived from it live in. Implements ``os.PathLike``, so it drops into ``open()``, ``Path()``,
    etc.

    Path semantics follow pathlib (str() is the normalised spelling: ``a//b/`` -> ``a/b``);
    ``file://`` URIs are accepted as input (RFC 8089, decoded like ``Path.from_uri``) and
    ``.uri`` renders an absolute path per RFC 3986 (``file:///a%20b``).

    Engines (one header each under adapter/engines/, discovered by the build):
    - json: JSON via simdjson (strict, SIMD-accelerated); writes real JSON and rejects non-finite
      floats.
    - jsonl: JSON Lines (ndjson) via simdjson's document stream: reads as a list, one item per
      document, and writes one compact document per line: a list's items, or one document.
    - toml: TOML via toml++: documents are tables (mapping root), there is no null, and dates and
      times are datetime objects (tomllib parity).
    - yaml: YAML 1.2 (core schema) via rapidyaml: anchors, aliases and merge keys are resolved;
      strings that would read back typed are quoted on write."""
    def __init__(self, path: str | bytes | os.PathLike[str], engine: Engine | None = None,
                 *, store: PathStore | None = None) -> None: ...

    # -- decoding / encoding ------------------------------------------------
    def read(self, engine: Engine | None = None, key_cache: int = 256) -> Any:
        """Decode the file to native Python objects (I/O and parsing release the GIL). engine=
        overrides for this call (json/jsonl/toml/yaml, a library label, or an alias); key_cache
        bounds the key-interning cache (0 off, -1 unbounded)."""
    def write(self, obj: Any, engine: Engine | None = None, *, atomic: bool = False, append: bool = False,
              parents: bool = False) -> None:
        """Serialise obj to this path with the resolved engine (json/jsonl/toml/yaml). Strings that
        would read back typed are quoted automatically, so write/read round-trips. atomic=True
        writes a hidden temporary sibling, forces it to the disk and renames it over the file: a
        reader sees the old contents or the new, never part of either, and after a power cut one
        of them is there whole. The file is then a new one (a second hard link keeps the old
        contents; a symlink is written through), which is why a plain write, like pathlib's,
        writes in place. append=True adds obj's documents to the file's end, the file made when
        it is not there, for an engine whose documents are lines (jsonl); the others refuse it,
        and so does atomic. parents=True makes the folders the file needs first. Format
        constraints are the engine's own:
        - json: JSON via simdjson (strict, SIMD-accelerated); writes real JSON and rejects
          non-finite floats.
        - jsonl: JSON Lines (ndjson) via simdjson's document stream: reads as a list, one item
          per document, and writes one compact document per line: a list's items, or one
          document.
        - toml: TOML via toml++: documents are tables (mapping root), there is no null, and dates
          and times are datetime objects (tomllib parity).
        - yaml: YAML 1.2 (core schema) via rapidyaml: anchors, aliases and merge keys are
          resolved; strings that would read back typed are quoted on write."""
    def read_bytes(self) -> bytes:
        """The raw file bytes, undecoded."""
    def write_bytes(self, data: bytes, *, atomic: bool = False) -> None:
        """Replace the file's contents with the given bytes; atomic=True as for write()."""
    def mkdir(self, parents: bool = False, exist_ok: bool = False) -> None:
        """Create this directory, like pathlib: parents= creates missing ancestors, exist_ok=
        tolerates an existing directory."""
    def unlink(self, missing_ok: bool = False) -> None:
        """Remove this file or symbolic link, like pathlib: missing_ok= tolerates nothing being
        there."""

    @property
    def engine(self) -> str | None:
        """The engine (library) read()/write() will use — the constructor pin, else the extension
        ('simdjson'/'simdjson-ndjson'/'toml++'/'rapidyaml') — or None when neither resolves."""
    @property
    def store(self) -> PathStore:
        """A PathStore over this path's table (the one derived paths share)."""

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
        """The RFC 3986 file URI of an absolute path (percent-encoded: file:///a%20b,
        file://host/share/x); a relative path keeps the 'file://<path>' spelling."""
    @property
    def name(self) -> str:
        """The final path component."""
    @property
    def stem(self) -> str:
        """The final component without its suffix."""
    @property
    def suffix(self) -> str:
        """The final extension incl. dot ('.yaml')."""
    @property
    def suffixes(self) -> list[str]:
        """All extensions of the final component ('.tar.gz' -> ['.tar','.gz'])."""
    @property
    def parts(self) -> list[str]:
        """The path components as a list."""
    @property
    def depth(self) -> int:
        """Named components below the anchor."""

    # -- composition / derived paths (results share the table and inherit the pin) --
    def __truediv__(self, other: str | bytes | os.PathLike[str]) -> path: ...
    def __rtruediv__(self, other: str | bytes | os.PathLike[str]) -> path: ...
    def joinpath(self, *parts: str | bytes | os.PathLike[str]) -> path:
        """Append one or more path components, like pathlib's joinpath()."""
    @property
    def parent(self) -> path:
        """The parent directory (pathlib's rule: the anchor is its own parent)."""
    @property
    def parents(self) -> list[path]:
        """Ancestor directories, closest first."""
    def with_suffix(self, suffix: str) -> path:
        """A copy with the final suffix replaced."""
    def with_name(self, name: str) -> path:
        """A copy with the final component replaced."""
    def with_stem(self, stem: str) -> path:
        """A copy with the stem replaced (suffix kept)."""
    def relative_to(self, other: str | bytes | os.PathLike[str], /) -> path:
        """This path with *other* taken off its front, like pathlib's relative_to():
        `path('/a/b/c')` relative to '/a' is `b/c`, and a path relative to itself is '.'. *other*
        is text, an os.PathLike or a path. Raises ValueError when this path is neither *other*
        nor beneath it. Compared as written, as == compares: on Windows letter case is not
        folded."""
    def is_relative_to(self, other: str | bytes | os.PathLike[str], /) -> bool:
        """Whether relative_to(other) would succeed: this path is *other* or beneath it."""
    def absolute(self) -> path:
        """An absolute copy (does not resolve symlinks/..)."""
    def resolve(self, strict: bool = False) -> path:
        """pathlib's resolve(): an absolute copy, its symlinks and '..' resolved; strict, a missing part is FileNotFoundError."""
    def read_text(self, encoding: str = 'utf-8', errors: str | None = None, newline: str | None = None) -> str:
        """pathlib's read_text(): the file decoded (UTF-8 unless encoding says, whatever the locale, as Python 3.15 does), every line ending read as \\n unless newline is given."""
    def write_text(self, data: str, encoding: str = 'utf-8', errors: str | None = None, newline: str | None = None,
                   *, atomic: bool = False) -> int:
        """pathlib's write_text(): data encoded (UTF-8 unless encoding says), each \\n written as os.linesep unless newline is given; the characters written. atomic=True as for write()."""
    def expanduser(self) -> path:
        """pathlib's expanduser(): a leading ~ or ~user replaced by that user's home; RuntimeError when no home is known."""

    # -- filesystem status --------------------------------------------------
    def is_absolute(self) -> bool:
        """Whether the path is absolute."""
    def exists(self) -> bool:
        """Whether the path exists on disk."""
    def is_file(self) -> bool:
        """Whether it is a regular file."""
    def is_dir(self) -> bool:
        """Whether it is a directory."""
    def is_symlink(self) -> bool:
        """Whether it is a symbolic link."""
    def size(self) -> int:
        """File size in bytes, a link followed; FileNotFoundError when it is not there, IsADirectoryError for a
        folder."""

    # -- directory traversal ------------------------------------------------
    def iterdir(self) -> list[path]:
        """The directory's children, sorted."""
    def glob(self, pattern: str, *, case_sensitive: bool | None = None) -> list[path]:
        """What pathlib's glob finds (recurse_symlinks=False): * ? and [seq] within a name (dot
        names too), ** across directories, yielding the one it starts in and never going through
        a link; a trailing '/' keeps directories; '.' is dropped, '..' a step up. case_sensitive:
        None as the system (any case on Windows), False any case. Sorted and deduplicated;
        results inherit the engine pin."""
    def rglob(self, pattern: str, *, case_sensitive: bool | None = None) -> list[path]:
        """glob('**/' + pattern): the pattern anywhere under this directory."""
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
    """Many paths as ONE table: every distinct path component is stored once and every path is a row
    (parent, name) in a hash-consed trie, so a path costs a few bytes plus its share of the
    unique names. Iterating yields path objects over the table (nothing copied); scan() reuses
    one path object per pass. filter_suffix()/filter_name() and |, &, - work on rows and return
    sets sharing the table; `ps & Filter` is a lazy Query; + and - take a PathSet or path(s); ==
    compares as sets; to_list() renders the members. PathSet(paths, store=s) shares the store's
    table, so its members and path(text, store=s) objects are rows of one table. Sets are built,
    never edited: `ps -= x` rebinds the name to a new set. Insertion order, append-only."""
    def __init__(self, paths: PathLike | Iterable[PathLike] | None = None,
                 *, store: PathStore | None = None) -> None:
        """PathSet(paths=None, *, store=None): `paths` is one path (str, bytes, os.PathLike, file,
        pathview) or an iterable of them."""
    @staticmethod
    def cwd() -> PathSet:
        """The set holding the current working directory."""
    def __bool__(self) -> bool: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    def clone(self) -> PathSet:
        """A distinct set object with the same members over the same table."""
    def add(self, path: PathLike) -> None: ...
    def extend(self, paths: Iterable[PathLike]) -> None: ...
    def reserve(self, n: int) -> None:
        """Size the table for `n` more paths up front (what a sized iterable already does)."""
    def __len__(self) -> int: ...
    def __contains__(self, path: object) -> bool: ...
    def __iter__(self) -> Iterator[path]: ...
    def scan(self) -> Iterator[path]:
        """Iterate reusing ONE path object (its row advances): no allocation per element. Do not
        keep the yielded object across iterations."""
    def __getitem__(self, i: int) -> path: ...
    def filter_suffix(self, suffix: str) -> PathSet:
        """The members whose final suffix equals `suffix` (pathlib's rule; case-sensitive)."""
    def filter_name(self, glob: str) -> PathSet:
        """The members whose name matches the glob (`*` and `?`)."""
    def filter_absolute(self) -> PathSet: ...
    def filter(self, filter: Filter) -> PathSet:
        """The members a Filter accepts, as a set over the same table."""
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
    def read_all_files(self, *, threads: int | None = None) -> list[str]:
        """The text of every member that is a regular file (UTF-8), one str per file, in member
        order; directories, missing paths, link loops, FIFOs and devices are left out, as
        pathlib's is_file() says — a FIFO or device is opened (non-blocking) to be told apart,
        where is_file() opens nothing. The reads run in C++ with the GIL released, as read_each()
        reads. threads: how many read at once; None starts with one and adds threads while the
        rest is worth them — up to the cores for reads that compute (a warm cache), past them for
        reads that wait (a cold disk, a network mount). A file the OS refuses raises as pathlib
        does; bytes that are not UTF-8 raise ValueError naming the first such member."""
    def read_each(self, *, threads: int | None = None, ahead_bytes: int = 64 * 2**20) -> Iterator[FileContent]:
        """Each member that is a regular file, as a FileContent in member order: a view of its
        bytes, held in C++ — memoryview(content) copies nothing; text() decodes UTF-8; .path
        names the member. Threads read ahead with the GIL released while you work on what they
        have read, at most 1,024 files and about ahead_bytes bytes ahead (a file is started while
        less than that waits). threads: as read_all_files. Folders, missing paths, link loops,
        FIFOs and devices are left out (a FIFO or device opened, non-blocking, to be told apart);
        a file the OS refuses raises in its place, and the reading ends there."""
    def sizes(self) -> list[int]:
        """Each member's size in bytes, in member order, as path.size() gives it (a link followed); the
        stats run with the GIL released. A member that is not there, or is not a file, raises the
        OSError path.size() raises, naming it."""
    def count_union(self, other: PathSet) -> int:
        """len(self | other) without building the set: a popcount over the bitmaps when the tables
        are shared."""
    def count_intersection(self, other: PathSet) -> int:
        """len(self & other) without building the set."""
    def count_difference(self, other: PathSet) -> int:
        """len(self - other) without building the set."""
    def relative_to(self, other: str | bytes | os.PathLike[str], /) -> PathSet:
        """Every member with *other* taken off its front, like pathlib's relative_to() per path, as
        a set over the same table in member order: `path(d).pathset('**/*').relative_to(d)` names
        everything beneath d from d. Raises ValueError naming the first member that is neither
        *other* nor beneath it."""
    def to_list(self) -> list[str]: ...
    def stats(self) -> dict[str, int]: ...


class Filter:
    """A predicate over the paths of a PathSet — made by ext(), name(), absolute() — with & (both),
    | (either) and ~ (not). Apply it with `ps & f` (a lazy Query) or ps.filter(f). Filters read
    the table, never the filesystem."""
    def __and__(self, other: Filter) -> Filter: ...
    def __or__(self, other: Filter) -> Filter: ...
    def __invert__(self) -> Filter: ...


class Query:
    """A lazy filter over a PathSet: `ps & f`. `& g` narrows, `| g` widens; eval() returns the
    matching members as a PathSet over the same table; iterating or len() evaluates it. The query
    keeps its source set alive."""
    def __and__(self, other: Filter) -> Query: ...
    def __or__(self, other: Filter) -> Query: ...
    def eval(self) -> PathSet:
        """The matching members as a PathSet over the same table."""
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
    so memoryview(content) copies nothing. bytes(content) copies; text() decodes UTF-8; .path is
    the member."""
    @property
    def path(self) -> path:
        """The member it was read from."""
    def __len__(self) -> int: ...
    def __bytes__(self) -> bytes: ...
    def __buffer__(self, flags: int, /) -> memoryview: ...
    def text(self) -> str:
        """The bytes decoded as UTF-8; ValueError naming the file when they are not."""

class PathStore:
    """A table of paths: every distinct component once, every path a row. path(text, store=s)
    interns into s's table and every path derived from that one stays there, so a store is how a
    program scopes a lifetime — pass one, keep it, drop it. Rows are never freed within a table;
    the table is freed with its last handle. PathStore() is a fresh table; default_store() is the
    one path(text) uses."""
    def __init__(self) -> None: ...
    def reserve(self, n: int) -> None:
        """Room for `n` more paths without rehashing."""
    def stats(self) -> dict[str, int]:
        """rows and segments in the table, and the bytes it holds."""
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
