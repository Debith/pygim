# -*- coding: utf-8 -*-
'''
'''

__all__ = ('type_error_msg', 'file_error_msg')


def type_error_msg(obj, expected_type):
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
    'Expected to get type `tuple,list`, got `[] [list]`'
    """
    if isinstance(expected_type, tuple):
        type_names = ",".join(f"{t.__name__}" for t in expected_type)
    else:
        type_names = expected_type.__name__
    return f"Expected to get type `{type_names}`, got `{repr(obj)} [{type(obj).__name__}]`"


def file_error_msg(filename, msg="not found"):
    return f"Expected filename `{str(filename.resolve().absolute())}` {msg}!"