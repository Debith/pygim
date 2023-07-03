# -*- coding: utf-8 -*-
'''
Internal package for file utils.
'''

from pathlib import Path
from .._iterlib import flatten, is_container


def flatten_paths(paths, pattern):
    if isinstance(paths, Path):
        if paths.is_dir():
            yield paths
            ps = list(paths.rglob(pattern))
            yield from ps
        else:
            yield paths
    else:
        assert is_container(paths), f'Expected `iterable` got `{type(paths).__name__}`'
        for path in flatten(paths):
            yield from flatten_paths(Path(path), pattern)