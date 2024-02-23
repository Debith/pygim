# -*- coding: utf-8 -*-
'''
This module implmements utilities to convert string to different format.
'''

from dataclasses import dataclass
from _pygim._support._case_builder import CaseBuilder

import re

__all__ = ["to_snake_case", "to_human_case", "to_kebab_case",
           "to_pascal_case", "to_camel_case"]


_CASE_BUILDER = CaseBuilder()


def to_snake_case(text):
    """
    Converts a string to snake case.

    Examples
    --------
    >>> to_snake_case("HelloWorld")
    'hello_world'

    >>> to_snake_case("hello World")
    'hello_world'

    Parameters
    ----------
    text : str
        The string to be converted.

    Returns
    -------
    str
        The snake case string.
    """
    return _CASE_BUILDER.set_text(text).adjust().lower().trim().replace().build('_')


def to_human_case(text):
    """
    Converts a string to human readable string.

    Example
    -------
    >>> to_human_case("HelloWorld")
    'Hello World'

    Parameters
    ----------
    text : str
        The string to be converted.

    Returns
    -------
    str
        The human readable string.
    """
    return _CASE_BUILDER.set_text(text).adjust().capitalize().trim().replace().build(' ')


def to_kebab_case(text):
    """
    Converts a string to kebab case.

    Parameters
    ----------
    text : str
        The string to be converted.

    Returns
    -------
    str
        The kebab case string.
    """
    return _CASE_BUILDER.set_text(text).adjust().lower().trim().replace().build('-')


def to_pascal_case(text):
    """
    Converts a string to pascal case.

    Examples
    --------
    >>> to_pascal_case("hello_world")
    'HelloWorld'

    >>> to_pascal_case("hello_world")
    'HelloWorld'

    Parameters
    ----------
    text : str
        The string to be converted.

    Returns
    -------
    str
        The pascal case string.
    """
    return _CASE_BUILDER.set_text(text).adjust().capitalize().trim().replace().build('')


def to_camel_case(text):
    """
    Converts a string to camel case.

    Examples
    --------
    >>> to_camel_case("hello_world")
    'helloWorld'

    >>> to_camel_case("hello_world")
    'helloWorld'

    Parameters
    ----------
    text : str
        The string to be converted.

    Returns
    -------
    str
        The camel case string.
    """
    pascal_cased_text = to_pascal_case(text)
    return pascal_cased_text[0].lower() + pascal_cased_text[1:]


if __name__ == "__main__":
    import doctest
    doctest.testmod()