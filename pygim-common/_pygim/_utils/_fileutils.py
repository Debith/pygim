# -*- coding: utf-8 -*-
'''
Internal package for file utils.
'''

import re
import fnmatch
from pathlib import Path
from .._iterlib import flatten


def flatten_paths(*paths, pattern):
    regex_pattern = fnmatch.translate(pattern)
    for path in flatten(paths):
        path = Path(path)

        if path.is_dir():
            if re.match(regex_pattern, str(path)):
                yield path
            ps = list(path.rglob(pattern))
            yield from ps
        else:
            yield path
