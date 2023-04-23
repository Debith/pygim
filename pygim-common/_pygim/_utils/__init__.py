# -*- coding: utf-8 -*-
'''
Internal utilities package.
'''

from ._iterable import *
from ._complaints import *


def format_dict(dct, *, indent=0):
    indention = " " * indent
    lines = [''] + [f"{indention}{key}={repr(value)}," for key, value in dct.items()] + ['']
    formatted_string = "\n".join(lines)

    return formatted_string