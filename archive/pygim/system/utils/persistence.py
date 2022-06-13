# -*- coding: utf-8 -*-
"""
"""

import pathlib
import gzip
import pickle


def quick_write_bytes(filename, data, *, ensure_parent_exist=False):
    filename = pathlib.Path(filename)
    parent = filename.parent

    if ensure_parent_exist and not parent.isdir():
        parent.mkdir(parents=True, exist_ok=True)

    assert filename.parent.is_dir(), f'Parent dir does not exist for file: {str(filename)}'
    filename.write_bytes(data)


def pickle_and_compress(obj, filename=None. *, ensure_parent_exist=False):
    data = gzip.compress(pickle.dumps(obj))
    if filename is not None:
        quick_write_bytes(filename, data, ensure_parent_exist=ensure_parent_exist)

    return data


def decompress_and_unpickle(obj):
    if isinstance(obj, pathlib.Path):
        obj = obj.read_bytes()
    return pickle.loads(gzip.decompress(obj))
