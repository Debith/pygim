# -*- coding: utf-8 -*-
"""
This module holds the dispatcher function implementation.
"""

from _pygim._magic._dispatcher import _Dispatcher

__all__ = ["dispatch"]

dispatch = _Dispatcher
dispatch.__doc__ = """
    A dispatcher that routes calls to different functions depending on the type of arguments.

    Parameters
    ----------
    callable : `object`
        A callable object for which a dispatcher is needed.
    registry : `dict`, optional
        A dictionary of functions mapped to specific argument types.
    args : `tuple`, optional
        A tuple of functions (argument type identifiers) for the dispatcher.
    start_index : `int`, optional
        An integer that defines the starting index of the method call.

    Methods
    -------
    register(*specs)
        Register a function with specific argument types.
"""