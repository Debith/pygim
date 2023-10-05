# -*- coding: utf-8 -*-
"""
Useful tools to write something into disk.
"""

import pathlib
import gzip
import pickle

from _pygim._error_msgs import type_error_msg

__all__ = (
    "write_bytes",
    "write_text",
    "pickle_and_compress",
    "decompress_and_unpickle",
    "read_text",
    "read_bytes",
    )


def _drop_file_suffixes(p):
    while p.suffixes:
        p = p.with_suffix("")
    return p


def _ensure_file(filename, use_dir, make_dirs):
    if use_dir:
        use_dir = pathlib.Path(use_dir)

        if use_dir.is_file():
            use_dir = use_dir.parent

    pth = pathlib.Path(filename)
    parent = pth.parent

    if use_dir:
        parent = use_dir.resolve() / parent

    if make_dirs and not parent.is_dir():
        parent.mkdir(parents=True, exist_ok=True)

    assert parent.is_dir(), f"Parent directory doesn't exist for file: {str(pth.resolve())}"

    return parent / pth.name


def write_bytes(filename, data, *, use_dir=None, make_dirs=False, suffix=".bin"):
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
    use_dir : `str` or `pathlib.Path`, optional
        The directory to use when writing the file. If specified, it overrides the parent
        directory of the `filename` parameter.
        Defaults to `None`, which means that the current working directory will be used
        or the parent directory of the file if the `filename` contains a path.
    make_dirs : `bool`, optional
        Create any necessary folders to allow writing. Defaults to `False`.
    suffix : `str`, optional
        The file suffix to use when writing the file. Defaults to `.bin`.

    Returns
    -------
    `pathlib.Path`
        The path to the file that was written.

    Examples
    --------
    Write a bytes object to a file:

    .. code-block:: python

        data = b"Hello, world!"
        write_bytes("hello.bin", data)
    """
    assert isinstance(data, bytes), "Data parameter must be a bytes object."

    target_file = _ensure_file(filename, use_dir, make_dirs)

    if suffix:
        target_file = _drop_file_suffixes(target_file).with_suffix(suffix)

    target_file.write_bytes(data)
    return target_file


def write_text(filename, data, *, use_dir=None, make_dirs=False, suffix=".txt", encoding="utf-8"):
    """
    Write text data to a file.

    This function provides a straightforward means of writing text data to a file by passing

    Parameters
    ----------
    filename : `str`
        Name of the file to be written.
    data : `str` or `list`
        Data to be written to the file.
    use_dir : `str` or `pathlib.Path`, optional
        The directory to use when writing the file. If specified, it overrides the parent
        directory of the `filename` parameter.
        Defaults to `None`, which means that the current working directory will be used
        or the parent directory of the file if the `filename` contains a path.
    make_dirs : `bool`, optional
        Create any necessary folders to allow writing. Defaults to `False`.
    suffix : `str`, optional
        The file suffix to use when writing the file. Defaults to `.txt`.
    encoding : `str`, optional
        The encoding to use when writing the file. Defaults to `utf-8`.

    Returns
    -------
    `pathlib.Path`
        The path to the file that was written.
    """
    assert isinstance(data, (str, list)), type_error_msg(data, str)

    if isinstance(data, list):
        data = "\n".join(data)

    target_file = _ensure_file(filename, use_dir, make_dirs)

    if suffix:
        target_file = _drop_file_suffixes(target_file).with_suffix(suffix)

    target_file.write_text(data)
    return target_file


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


def read_text(filename, *, use_dir=None, encoding="utf-8"):
    """
    Reads text data from a file.

    Parameters
    ----------
    filename : `str`
        Name of the file to be read.
    use_dir : `str` or `pathlib.Path`, optional
        The directory to use when reading the file. If specified, it overrides the parent
        directory of the `filename` parameter.
        Defaults to `None`, which means that the current working directory will be used
        or the parent directory of the file if the `filename` contains a path.
    encoding : `str`, optional
        The encoding to use when reading the file. Defaults to `utf-8`.

    Returns
    -------
    `str`
        The text data read from the file.

    Examples
    --------
    Read text data from a file:

    .. code-block:: python

        data = read_text("hello.txt")
    """
    source_file = _ensure_file(filename, use_dir, False)
    return source_file.read_text(encoding=encoding)


def read_bytes(filename, *, use_dir=None):
    """
    Reads binary data from a file.

    Parameters
    ----------
    filename : `str`
        Name of the file to be read.
    use_dir : `str` or `pathlib.Path`, optional
        The directory to use when reading the file. If specified, it overrides the parent
        directory of the `filename` parameter.
        Defaults to `None`, which means that the current working directory will be used
        or the parent directory of the file if the `filename` contains a path.

    Returns
    -------
    `bytes`
        The binary data read from the file.

    Examples
    --------
    Read binary data from a file:

    .. code-block:: python

        data = read_bytes("hello.bin")
    """
    source_file = _ensure_file(filename, use_dir, False)
    return source_file.read_bytes()