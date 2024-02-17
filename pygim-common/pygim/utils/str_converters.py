# -*- coding: utf-8 -*-
'''
This module implmements utilities to convert string to different format.
'''

import re


__all__ = ["to_snake_case", "to_human_case", "to_kebab_case", "to_pascal_case"]


def to_snake_case(str):
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
    str : str
        The string to be converted.

    Returns
    -------
    str
        The snake case string.
    """
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', str)
    s2 = re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()
    return s2.replace(' ', '')


def to_human_case(str):
    """
    Converts a string to human readable string.

    Example
    -------
    >>> to_human_case("HelloWorld")
    'Hello World'

    Parameters
    ----------
    str : str
        The string to be converted.

    Returns
    -------
    str
        The human readable string.
    """
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1 \2', str)
    return re.sub('([a-z0-9])([A-Z])', r'\1 \2', s1).title().replace("_", " ")


def to_kebab_case(str):
    """
    Converts a string to kebab case.

    Parameters
    ----------
    str : str
        The string to be converted.

    Returns
    -------
    str
        The kebab case string.
    """
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1-\2', str)
    return re.sub('([a-z0-9])([A-Z])', r'\1-\2', s1).lower()


def to_pascal_case(str):
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
    str : str
        The string to be converted.

    Returns
    -------
    str
        The pascal case string.
    """
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1\2', str)
    return re.sub('([a-z0-9])([A-Z])', r'\1\2', s1).title().replace('_', '')


if __name__ == "__main__":
    import doctest
    doctest.testmod()