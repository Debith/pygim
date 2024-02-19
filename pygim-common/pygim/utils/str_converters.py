# -*- coding: utf-8 -*-
'''
This module implmements utilities to convert string to different format.
'''

from dataclasses import dataclass, field

import re

__all__ = ["to_snake_case", "to_human_case", "to_kebab_case",
           "to_pascal_case", "to_camel_case"]


@dataclass
class _CaseBuilder:
    """ A class to convert a string to different case. """
    _word_separator: object = re.compile(r'[^a-zA-Z]+')
    _capital_detector: object = re.compile(r'(?<=[a-z])(?=[A-Z])')
    _trimmer: object = re.compile(r'^ | $')
    _replacer: object = re.compile(r'  +')

    def set_text(self, text):
        self._text = text
        return self

    def set_sep(self, sep):
        self._sep = sep
        return self

    def adjust(self):
        step1 = self._word_separator.sub(' ', self._text)
        self._text = self._capital_detector.sub(' ', step1)
        return self

    def lower(self):
        self._transform_func = str.lower
        return self

    def capitalize(self):
        self._transform_func = str.capitalize
        return self

    def trim(self):
        self._text = self._trimmer.sub('', self._text)
        return self

    def replace(self):
        self._text = self._replacer.sub(' ', self._text)
        return self

    def build(self, sep):
        return sep.join(self._transform_func(word) for word in self._text.split())


_CASE_BUILDER = _CaseBuilder()


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