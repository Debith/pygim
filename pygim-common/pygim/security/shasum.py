# -*- coding: utf-8 -*-
"""
This module checksum calculation helper.
"""

import hashlib
import pathlib

from _pygim._error_msgs import file_error_msg
from _pygim._exceptions import ShaSumTargetNotFoundError
from _pygim.typing import PathLike
from ..fileio.pathset import PathSet
from ..performance.dispatch import dispatch

__all__ = ["sha256sum", "sha256sum_file"]


@dispatch
def sha256sum(obj, *, encoding="utf-8"):
    """
    Compute the SHA256 sum for the given string.

    Parameters
    ----------
    obj : `Any`
        Object to be encoded.
    encoding : `str`, optional
        Encoding used to convert string objects into bytes. Defaults to "utf-8".

    Returns
    -------
    `str`
        Calculated SHA256 sum.

    Raises
    ------
    NotImplementedError
        If `sha256sum` is not implemented for the given object type.

    Examples
    --------
    >>> sha256sum("hello sha256!")
    '705cb95c164e32feec2aef56f70d73e064afe2e38d40e5189fc5f8cdc84a9eaf'
    """
    raise NotImplementedError(f"sha256sum not implemented for type: {type(obj)}")


@sha256sum.register(str)
def _(text: str, *, encoding="utf-8"):
    assert isinstance(text, str)
    return hashlib.sha256(text.encode(encoding)).hexdigest()


@sha256sum.register(bytes)
def _(text: bytes, **_):  # type: ignore
    assert isinstance(text, bytes)
    return hashlib.sha256(text).hexdigest()


@sha256sum.register(int)
@sha256sum.register(float)
def _(number, *, encoding="utf-8"):
    return sha256sum(str(number), encoding=encoding)


@sha256sum.register(list)
def _(items: list, **_):
    content = ",".join(sha256sum(i) for i in items)
    return sha256sum(f"[{content}]")


@sha256sum.register(pathlib.Path)
def sha256sum_file(filename: PathLike):
    filename = pathlib.Path(filename)
    if not filename.exists():
        raise ShaSumTargetNotFoundError(file_error_msg(filename))

    if filename.is_dir():
        raise NotImplementedError(f"Not implemented for dir: {str(filename)}")
    else:
        assert filename.is_file()
        return sha256sum(filename.read_bytes())


try:
    import numpy as np

    @sha256sum.register(np.float16)
    @sha256sum.register(np.float32)
    @sha256sum.register(np.float64)
    def _(number, *, encoding="utf-8"):
        return sha256sum(str(number), encoding=encoding)

    @sha256sum.register(np.str_)
    def _(text: np.str_, *, encoding="utf-8"):
        return sha256sum(text.encode(encoding))

    @sha256sum.register(np.ndarray)
    def _(items: np.array, **_):
        content = np.vectorize(sha256sum)(items)
        return sha256sum(f"{items.__class__.__name__}({content})")
except ImportError:
    pass


try:
    import pandas as pd

    @sha256sum.register(pd.Timestamp)
    def _(ts, **_):
        return sha256sum(ts.isoformat())

    @sha256sum.register(pd.Series)
    def _(series: pd.Series, **_):
        content = series.apply(sha256sum)
        return sha256sum(f"{series.__class__.__name__}({content})")

    ROWS, COLS = 0, 1
    @sha256sum.register(pd.DataFrame)
    def _(df: pd.DataFrame, *, axis=ROWS, **_):
        content = df.apply(sha256sum, axis=axis)
        return sha256sum(f"{df.__class__.__name__}({content})")

except ImportError:
    pass
