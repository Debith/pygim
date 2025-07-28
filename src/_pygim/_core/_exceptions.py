# -*- coding: utf-8 -*-
"""
This module contains all exceptions found and used in pygim.
"""

from ._error_msgs import type_error_msg
from dataclasses import dataclass
from . import _typing as t

__all__ = [
    'GimException',
    'GimError',
    'GimOptionError',
    'EntangledError',
    'EntangledClassError',
    'EntangledMethodError',
    'ShaSumTargetNotFoundError',
    'DispatchError',
    'UnrecognizedTypeError',
]

class GimException(Exception):
    """Generic exception that can be used across Python projects."""
    DEFAULT_MSG = "Gim Error."

    def __init__(self, msg=None, sep='\n'):
        assert isinstance(msg, (str, list, tuple))
        self._msg = msg or self.DEFAULT_MSG
        self._sep = sep

    def __str__(self):
        if isinstance(self._msg, str):
            return self._msg

        return self._sep.join(str(m) for m in self._msg)


class GimError(GimException):
    """Error used as a base class for all exceptions Python Gimmicks library."""


@dataclass
class GimOptionError(GimError):
    """ Raised when value not among options. """
    _input_value: t.Any
    _choices: t.Iterable

    def __str__(self):
        choices = ", ".join(str(c) for c in self._choices)
        return f"Given input ``{self._input_value}`` not among: {choices}"


class EntangledError(GimError):
    """Base class for entanglement errors."""


class EntangledClassError(EntangledError):
    """Raised when issue detected with entangled class."""


class EntangledMethodError(EntangledError):
    """Raised when issue detected with methods of entangled class."""


class ShaSumTargetNotFoundError(GimError, FileNotFoundError):
    """Raised when path for sha256sum not found."""


class DispatchError(GimError):
    """Base class for dispatch errors."""
    DEFAULT_MSG = "Dispatch error."


class UnrecognizedTypeError(DispatchError):
    """Raised when a type is not recognized."""
    def __init__(self, given_type, expected_types):
        super().__init__(type_error_msg(given_type, expected_types))