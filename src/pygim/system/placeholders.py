# -*- coding: utf-8 -*-
"""
This module contains simple replacements
"""

__all__ = ['__not_implemented__', 'Unset']


def __not_implemented__(*_, **__):
    raise NotImplementedError()


class UnsetType(type):
    def __repr__(self):
        return "Unset"

    def __bool__(self):
        return False


class Unset(metaclass=UnsetType): pass