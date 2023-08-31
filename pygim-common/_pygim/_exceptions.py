# -*- coding: utf-8 -*-
"""
This module contains all exceptions found and used in pygim.
"""


class GimError(Exception):
    """Main error class."""


class NoArgumentsError(GimError):
    """Raised when function does not receive arguments while expecting them."""


class EntangledError(GimError):
    """Base class for entanglement errors."""


class EntangledClassError(EntangledError):
    """Raised when issue detected with entangled class."""


class EntangledMethodError(EntangledError):
    """Raised when issue detected with methods of entangled class."""
