# -*- coding: utf-8 -*-
"""Error messages pygim's exceptions share."""

import pathlib

__all__ = ("type_error_msg", "file_error_msg")


def type_error_msg(obj_or_type, expected_type):
    """
    Returns a formatted error message for a type error.

    Parameters
    ----------
    obj : Any
        The object that was found to have a type error.
    expected_type : type or tuple of types
        The expected type(s) of the object.

    Returns
    -------
    str
        The formatted error message.

    Examples
    --------
    >>> type_error_msg(2, str)
    'Expected to get type `str`, got `2 [int]`'
    >>> type_error_msg([], (tuple, list))
    'Expected to get type `tuple, list`, got `[] [list]`'
    """
    if isinstance(expected_type, tuple):
        type_names = []
        for t in expected_type:
            if t is None:
                type_names.append("None")
            else:
                type_names.append(t.__name__)
        type_names = ", ".join(type_names)
    else:
        type_names = expected_type.__name__

    if isinstance(obj_or_type, type):
        return f"Expected to get type `{type_names}`, got `{obj_or_type.__name__}`"
    else:
        return f"Expected to get type `{type_names}`, got `{repr(obj_or_type)} [{type(obj_or_type).__name__}]`"


def file_error_msg(filename, msg="not found"):
    """
    Returns a formatted error message naming a file by its absolute, resolved path.

    Parameters
    ----------
    filename : str or os.PathLike
        The file the message is about.
    msg : str
        What is wrong with it (default: "not found").

    Examples
    --------
    >>> file_error_msg(pathlib.Path("/tmp/x.txt"))  # doctest: +SKIP
    'Expected filename `/tmp/x.txt` not found!'
    """
    return f"Expected filename `{pathlib.Path(filename).resolve()}` {msg}!"
