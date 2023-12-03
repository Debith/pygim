# -*- coding: utf-8 -*-
'''
Internal utilities package.
'''

from .._iterlib import *
from ._inspect import *


def format_dict(dct, *, indent=0, value_formatter=repr):
    """
    Format a dictionary, including nested dictionaries, into a string.

    Parameters
    ----------
    dct : dict
        The dictionary to be formatted.
    indent : int, optional
        The initial indentation level (default is 0).

    Returns
    -------
    str
        The formatted string representation of the dictionary.

    Examples
    --------
    >>> data = {"key1": "value1", "key2": {"nestedKey1": "nestedValue1"}}
    >>> print(format_dict(data))
    key1='value1',
    key2=
     nestedKey1='nestedValue1',
    """
    indention = " " * indent
    lines = []

    for key, value in dct.items():
        if isinstance(value, dict) and value:
            nested = format_dict(value, indent=indent + 4, value_formatter=value_formatter)
            lines.append(f"{indention}{key}=\n{nested},")
        else:
            try:
                lines.append(f"{indention}{key}={value_formatter(value)}")
            except Exception:
                try:
                    lines.append(f"{indention}{key}={repr(value)}")
                except Exception:
                    lines.append(f"{indention}{key}={type(value).__name__}")

    formatted_string = "\n".join(lines)

    return formatted_string
