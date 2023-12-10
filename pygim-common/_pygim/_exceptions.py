# -*- coding: utf-8 -*-
"""
This module contains all exceptions found and used in pygim.
"""

from ._error_msgs import type_error_msg

class GimError(Exception):
    """Main error class."""
    DEFAULT_MSG = "Gim error."

    def __init__(self, msg=None):
        super().__init__(msg or self.DEFAULT_MSG)


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