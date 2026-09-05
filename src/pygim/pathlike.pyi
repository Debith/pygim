"""Type stubs for pygim.pathlike.

The compiled extension is untyped at runtime; this stub documents the stable
public contract of ``pygim.path(...)`` / ``pathlike.file``.

The engine set is OPEN (one C++ header per engine, discovered by the build), so
everything engine-dependent — the ``Engine`` selector literal, the
``<name>file`` typed classes and ``ENGINES`` — lives in the generated block at
the end, rendered from the built module by ``pygim stubs`` and kept current by
a test.
"""

import os
from typing import Any, Literal, NamedTuple

def path(path: str | os.PathLike[str], engine: Engine | None = None) -> file:
    """Wrap a path in a self-reading, self-decoding file().

    ``path`` is native path text (str, bytes or os.PathLike) or a ``file://`` URI
    (RFC 8089, decoded like ``pathlib.Path.from_uri``: ``file:///abs/x``,
    ``file://localhost/abs/x``, ``file://host/share/x``); a relative file URI or
    any other scheme raises ValueError. ``engine=`` pins the decoder for this
    object (inherited by derived paths); the default resolves from the file
    extension at read/write time.
    """

class file(os.PathLike[str]):
    def __init__(self, path: str | os.PathLike[str], engine: Engine | None = None) -> None: ...

    # -- decoding / encoding ------------------------------------------------
    def read(self, engine: Engine | None = None, key_cache: int = 256) -> Any:
        """Decode the file to native Python objects (GIL released during I/O
        and parsing). ``key_cache`` bounds key interning: 0 off, -1 unbounded."""
    def write(self, obj: Any, engine: Engine | None = None) -> None:
        """Serialise ``obj`` with the resolved engine. Strings that would read
        back typed are quoted, so write/read round-trips. Format constraints are
        the engine's own (see each ``<name>file`` docstring)."""
    def read_bytes(self) -> bytes: ...
    def write_bytes(self, data: bytes) -> None:
        """Replace the file's contents with *data*, byte for byte."""
    def mkdir(self, parents: bool = False, exist_ok: bool = False) -> None:
        """Create this directory, with pathlib's parents/exist_ok semantics."""

    @property
    def engine(self) -> str | None:
        """The library label read()/write() will use ('rapidyaml', 'simdjson', ...):
        the constructor pin, else the extension — or None when neither resolves."""

    # -- os.PathLike / identity ---------------------------------------------
    def __fspath__(self) -> str:
        """The native path text in pathlib's normalised spelling ('a//b/' -> 'a/b')."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

    # -- name components (pathlib parity) -----------------------------------
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

    # -- composition / derived paths (results inherit the engine pin) -------
    def __truediv__(self, other: str | os.PathLike[str]) -> file: ...
    def __rtruediv__(self, other: str | os.PathLike[str]) -> file: ...
    def joinpath(self, *parts: str | os.PathLike[str]) -> file: ...
    @property
    def parent(self) -> file: ...
    @property
    def parents(self) -> list[file]: ...
    def with_suffix(self, suffix: str) -> file: ...
    def with_name(self, name: str) -> file: ...
    def with_stem(self, stem: str) -> file: ...
    def absolute(self) -> file: ...
    def resolve(self) -> file: ...

    # -- filesystem status --------------------------------------------------
    def is_absolute(self) -> bool: ...
    def exists(self) -> bool: ...
    def is_file(self) -> bool: ...
    def is_dir(self) -> bool: ...
    def is_symlink(self) -> bool: ...
    def size(self) -> int: ...

    # -- directory traversal ------------------------------------------------
    def iterdir(self) -> list[file]: ...
    def glob(self, pattern: str) -> list[file]: ...
    def rglob(self, pattern: str) -> list[file]: ...
    def pathset(self, pattern: str = "*") -> Any:
        """The glob results as a ``pygim.pathset.PathSet``."""

class EngineInfo(NamedTuple):
    """One entry of ``ENGINES``: the registry as data."""
    name: str
    label: str
    extensions: tuple[str, ...]
    aliases: tuple[str, ...]
    doc: str

# --- generated: engines (regenerate with `pygim stubs`) ---
# Selection accepts FORMAT names, LIBRARY labels and aliases; .engine reports the label.
Engine = Literal[
    "json", "simdjson",
    "jsonl", "simdjson-ndjson", "ndjson",
    "toml", "toml++", "tomlplusplus",
    "yaml", "rapidyaml", "yml",
]

class jsonfile(file):
    """JSON via simdjson (strict, SIMD-accelerated); writes real JSON and rejects non-finite floats. Constructing one pins the engine (simdjson)."""
    def __init__(self, path: str | os.PathLike[str]) -> None: ...

class jsonlfile(file):
    """JSON Lines (ndjson) via simdjson's document stream: reads as a list, one item per document, and writes one compact document per line from a list root. Constructing one pins the engine (simdjson-ndjson)."""
    def __init__(self, path: str | os.PathLike[str]) -> None: ...

class tomlfile(file):
    """TOML via toml++: documents are tables (mapping root), there is no null, and dates and times are datetime objects (tomllib parity). Constructing one pins the engine (toml++)."""
    def __init__(self, path: str | os.PathLike[str]) -> None: ...

class yamlfile(file):
    """YAML 1.2 (core schema) via rapidyaml: anchors, aliases and merge keys are resolved; strings that would read back typed are quoted on write. Constructing one pins the engine (rapidyaml)."""
    def __init__(self, path: str | os.PathLike[str]) -> None: ...

ENGINES: tuple[EngineInfo, ...]
# --- end generated ---
