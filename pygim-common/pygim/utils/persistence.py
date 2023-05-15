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
    """
    Write bytes data to a file.

    This function provides a straightforward means of writing bytes data to a file by passing
    the name of the file and the contents to write. It optionally creates any necessary folders
    to allow writing.

    Parameters
    ----------
    filename : `str`
        Name of the file to be written.
    data : `bytes`
        Data to be written to the file.
    make_dirs : `bool`, optional
        Create any necessary folders to allow writing. Defaults to `False`.
    suffix : `str`, optional
        The file suffix to use when writing the file. Defaults to `.bin`.

    Raises
    ------
    AssertionError
        If the `data` parameter is not a bytes object or if the parent directory
        doesn't exist and `make_dirs` is `False`.

    Examples
    --------
    Write a bytes object to a file:

    .. code-block:: python

        data = b"Hello, world!"
        write_bytes("hello.bin", data)
    """
    assert isinstance(data, bytes), "Data parameter must be a bytes object."

    pth = pathlib.Path(filename)
    parent = pth.parent

    if make_dirs and not parent.is_dir():
        parent.mkdir(parents=True, exist_ok=True)

    assert parent.is_dir(), f"Parent directory doesn't exist for file: {str(pth.resolve())}"

    if suffix:
        pth = _drop_file_suffixes(pth).with_suffix(suffix)

    pth.write_bytes(data)


def pickle_and_compress(obj, filename=None, *, make_dirs=False, suffix=".pkl.zip"):
    """
    Pickles and compresses an object, and writes it to a file.

    Parameters
    ----------
    obj : `object`
        The object to be pickled and compressed.
    filename : `str`, optional
        Name of the file to write the pickled and compressed object to. Defaults to None,
        which means that result is returned as bytes.
    make_dirs : `bool`, optional
        Create any necessary folders to allow writing. Defaults to False.
    suffix : `str`, optional
        The file suffix to use when writing the file. Defaults to ".pkl.zip".

    Returns
    -------
    `bytes`
        Pickled and compressed object in bytes.

    Notes
    -----
    This function uses the gzip module to compress the pickled object before writing it to a file.

    If `filename` is not specified, the pickled and compressed object will be returned as bytes.

    Examples
    --------
    Pickle and compress an object and write it to a file:

    .. code-block:: python

        my_list = [1, 2, 3, 4, 5]
        pickle_and_compress(my_list, filename="my_list.pkl.zip", make_dirs=True)

    Pickle and compress an object and return the bytes:
    >>> my_dict = {"name": "John", "age": 30}
    >>> data = pickle_and_compress(my_dict)
    """
    data = gzip.compress(pickle.dumps(obj))

    if filename is not None:
        write_bytes(filename, data, make_dirs=make_dirs, suffix=suffix)

    return data


def decompress_and_unpickle(obj):
    """
    Decompresses and unpickles a given object.

    Parameters
    ----------
    obj : `str` or `pathlib.Path`
        A byte object or a `Path` object used to read the data.

    Returns
    -------
    `object`
        The object returned by this procedure.

    Examples
    --------
    Load and unpickle a compressed file:

    .. code-block:: python

        obj = decompress_and_unpickle("file.pkl.zip")
    """
    if isinstance(obj, str):
        pth = pathlib.Path(obj)
        if pth.is_file():
            obj = pth

    if isinstance(obj, pathlib.Path):
        obj = obj.read_bytes()

    return pickle.loads(gzip.decompress(obj))
