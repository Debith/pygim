# -*- coding: utf-8 -*-
"""
Useful tools to write something into disk.
"""

import pathlib
import gzip
import pickle

import pygim.typing as t

__all__ = ("write_bytes", "pickle_and_compress", "decompress_and_unpickle")


def _drop_file_suffixes(p):
    while p.suffixes:
        p = p.with_suffix("")
    return p


def write_bytes(filename, data, *, make_dirs=False, suffix=".bin"):
    """Writes bytes into file.

    This function provides straight-forward means to write bytedata into a
    file by passing the name of the file and the contents to write. Additionally,
    by providing argument, it creates any necessary folders to allow writing.

    Args:
        filename (_type_): Name of the file to be written.
        data (bytes): Data to be written in file.
        make_dirs (bool, optional): Create folder structure. Defaults to False.
    """
    assert isinstance(data, bytes)
    pth: pathlib.Path = pathlib.Path(filename)
    parent = pth.parent

    if make_dirs and not parent.is_dir():
        parent.mkdir(parents=True, exist_ok=True)

    assert pth.parent.is_dir(), f"Parent dir does not exist for file: {str(pth.resolve())}"
    if suffix:
        pth = _drop_file_suffixes(pth).with_suffix(suffix)
    pth.write_bytes(data)


def pickle_and_compress(obj, filename=None, *, make_dirs, suffix=".pkl.zip"):
    """Writes pickled and compressed object into a file.

    Args:
        filename (_type_): Name of the file to be written.
        data (bytes): Data to be written in file.
        make_dirs (bool, optional): Create folder structure. Defaults to False.
        suffix (str, optional): File suffix used to write the file. Set as None, if
                                you include suffix in the filename, otherwise it is
                                replaced.

    Returns:
        (bytes): Pickled and compressed object in bytes.
    """
    data = gzip.compress(pickle.dumps(obj))
    if filename is not None:
        write_bytes(filename, data, make_dirs=make_dirs, suffix=suffix)

    return data


def decompress_and_unpickle(obj):
    """Decompress and unpickle given object.

    Args:
        obj (_type_): A byte object, or `Path` object used to read the data.

    Returns:
        (object): Object returned by this procedure.
    """
    if isinstance(obj, str):
        pth = pathlib.Path(obj)
        if pth.is_file():
            obj = pth

    if isinstance(obj, pathlib.Path):
        obj = obj.read_bytes()

    return pickle.loads(gzip.decompress(obj))
