# -*- coding: utf-8 -*-
"""
This module contains internal utility functions.
"""

__all__ = ("split", "flatten", "is_container")


def split(iterable, condition):
    left = []
    right = []

    for it in iterable:
        if condition(it):
            left.append(it)
        else:
            right.append(it)

    return left, right


def is_container(obj):
    if isinstance(obj, (str, bytes, type)):
        return False

    if hasattr(obj, "__iter__"):
        return True

    return isinstance(obj, memoryview)


def flatten(items):
    for subitem in items:
        if is_container(subitem):
            yield from flatten(subitem)
        else:
            yield subitem
