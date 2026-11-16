"""Type stubs for pygim.errors: every exception pygim raises, each a GimError and the Python
exception its contract needs. The classes are made once, in C++; every pygim extension raises
these same classes."""

from typing import Any, Iterable

__all__ = ("GimException", "GimError", "MissingKeyError", "GimOptionError", "DispatchError", "UnrecognizedTypeError")


class GimException(Exception):
    """The root of every exception pygim raises; GimError is the one to catch."""


class GimError(GimException):
    """Everything pygim raises is a GimError: `except GimError` catches it all."""


class MissingKeyError(GimError, KeyError):
    """A lookup found nothing: a key with no provider, a name never registered. A KeyError too, as a
    mapping's lookup raises."""


class GimOptionError(GimError, ValueError):
    """GimOptionError(value, options): a value none of its options. A ValueError too, as Python raises
    for a bad value; `value` and `options` say which."""

    value: Any
    options: tuple[Any, ...]

    def __init__(self, value: Any, options: Iterable[Any]) -> None: ...


class DispatchError(GimError):
    """A call could not be dispatched."""


class UnrecognizedTypeError(DispatchError):
    """UnrecognizedTypeError(given_type, expected_types): a value, or a type, that is none of the types
    expected."""

    def __init__(self, given_type: Any, expected_types: type | tuple[type | None, ...]) -> None: ...
