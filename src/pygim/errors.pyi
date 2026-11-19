"""Type stubs for pygim.errors: every exception pygim raises, each a GimError and the Python
exception its contract needs. The classes are made once, in C++; every pygim extension raises
these same classes. tests/unittests/test_errors.py holds this stub to the built module."""

from typing import Any, Iterable

__all__ = (
    "GimException",
    "GimError",
    "MissingKeyError",
    "GimOptionError",
    "DispatchError",
    "UnrecognizedTypeError",
    "ArgumentError",
    "SignatureError",
    "InvalidValueError",
    "ParseError",
    "InvalidStateError",
    "DuplicateKeyError",
    "CircularDependencyError",
    "UnsupportedColumnTypeError",
    "BackendError",
    "GimOSError",
    "GimFileNotFoundError",
    "GimFileExistsError",
    "GimNotADirectoryError",
    "GimPermissionError",
    "GimOverflowError",
    "IndexOutOfRangeError",
    "MissingAttributeError",
)


class GimException(Exception):
    """The root of every exception pygim raises; GimError is the one to catch."""


class GimError(GimException):
    """Everything pygim raises is a GimError: `except GimError` catches it all."""


class MissingKeyError(GimError, KeyError):
    """MissingKeyError(key, within=None): a lookup found nothing; `within` is where it looked. A KeyError too,
    as a mapping's lookup raises."""

    within: Any

    def __init__(self, key: Any, within: Any = None) -> None: ...


class GimOptionError(GimError, ValueError):
    """GimOptionError(value, options): a value none of its options. A ValueError too, as Python raises
    for a bad value; `value` and `options` say which."""

    value: Any
    options: tuple[Any, ...]

    def __init__(self, value: Any, options: Iterable[Any]) -> None: ...


class DispatchError(GimError, TypeError):
    """A call none of its implementations takes: none is registered for its arguments' types. A
    TypeError too, as Python raises when no implementation of an operator takes its operands."""


class UnrecognizedTypeError(GimError, TypeError):
    """UnrecognizedTypeError(given_type, expected_types, subject=None): a value, or a type, that is none of
    the types expected. A TypeError too, as Python raises for a value of the wrong type."""

    def __init__(
        self, given_type: Any, expected_types: str | type | None | tuple[type | None, ...], subject: Any = None
    ) -> None: ...


class ArgumentError(GimError, TypeError):
    """A call pygim cannot take as given: an unknown keyword, an argument missing or too many, two that
    exclude each other. A TypeError too, as Python raises for a call that does not fit."""


class SignatureError(GimError, TypeError):
    """A callable whose signature pygim cannot use: a positional-only parameter, an annotation missing,
    type hints that do not resolve. A TypeError too."""


class InvalidValueError(GimError, ValueError):
    """A value of the right type, outside what is accepted: a count below one, an empty name. A
    ValueError too."""


class ParseError(GimError, ValueError):
    """Text that is not its format, or bytes that are not UTF-8; the message names the file and line.
    A ValueError too, as json.JSONDecodeError is."""


class InvalidStateError(GimError, RuntimeError):
    """An operation the object's state does not allow: a lap on a finished timer. A RuntimeError too."""


class DuplicateKeyError(GimError, ValueError):
    """A key registered twice without override. A ValueError, not a KeyError: Mapping.get() and `in`
    catch a KeyError, and must not swallow this."""


class CircularDependencyError(GimError, ValueError):
    """Wiring that depends on itself. A ValueError too, as graphlib.CycleError is."""


class UnsupportedColumnTypeError(GimError, TypeError):
    """A column whose Arrow or SQL type pygim cannot map. A TypeError too."""


class BackendError(GimError, RuntimeError):
    """A library, driver or command pygim calls (ODBC, BCP, Arrow, a rebuild command) reported a
    failure; the message carries what it said. A RuntimeError too."""


class GimOSError(GimError, OSError):
    """GimOSError(errno, strerror, filename=None): the system refused an operation on a file, as
    pathlib raises it, or on a socket. An OSError too; the errnos Python names have a class of
    their own below it."""


class GimFileNotFoundError(GimOSError, FileNotFoundError):
    """No such file or directory. A FileNotFoundError too."""


class GimFileExistsError(GimOSError, FileExistsError):
    """The file or directory exists already. A FileExistsError too."""


class GimNotADirectoryError(GimOSError, NotADirectoryError):
    """Not a directory. A NotADirectoryError too."""


class GimPermissionError(GimOSError, PermissionError):
    """The system refused access. A PermissionError too."""


class GimOverflowError(GimError, OverflowError):
    """A quantity past what its type holds. An OverflowError too."""


class IndexOutOfRangeError(GimError, IndexError):
    """A position past the end. An IndexError too, which iteration by index stops at."""


class MissingAttributeError(GimError, AttributeError):
    """An attribute that is not there. An AttributeError too, which getattr(obj, name, default) and
    hasattr catch."""
