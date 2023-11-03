# -*- coding: utf-8 -*-
'''
Internal package for file utils.
'''

from pathlib import Path
from .._iterlib import flatten


def flatten_paths(*paths, pattern):
    for path in flatten(paths):
        path = Path(path)

        if path.is_dir():
            yield path
            ps = list(path.rglob(pattern))
            yield from ps
        else:
            yield path
