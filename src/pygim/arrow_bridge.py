"""Arrow/Polars interoperability helpers.

These utilities wrap the guidance from the Arrow IPC article referenced in the
project discussion: use Apache Arrow's columnar format as the transport bridge
between a Polars ``DataFrame`` (Python) and a C++ consumer.  The helpers are
implemented with *lazy imports* so the heavy dependencies (``polars`` and
``pyarrow``) remain optional until these functions are used.

Typical usage
-------------

.. code-block:: python

    import polars as pl
    from pygim.arrow_bridge import (
        to_arrow_table,
        to_ipc_bytes,
        write_ipc_file,
        read_ipc_file,
    )

    df = pl.DataFrame({"id": [1, 2], "name": ["Ada", "Grace"]})

    # Zero-copy hand-off into a PyArrow Table.
    table = to_arrow_table(df)

    # Persist to Feather/IPC so a C++ process can memory-map it.
    write_ipc_file(df, "data.arrow")

    # Or keep the payload in-memory for streaming.
    ipc_bytes = to_ipc_bytes(df)

    # Later, recover a PyArrow table (e.g. inside a C++ integration test) and
    # pass it to Arrow's C++ APIs.
    table_from_disk = read_ipc_file("data.arrow")

The helpers deliberately expose ``pyarrow`` objects instead of trying to hide
them.  That keeps the hand-off explicit and lets downstream C++/Python code use
Arrow's rich APIs directly (C Data Interface, Flight, etc.).
"""
from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Iterable, Optional, Union

if TYPE_CHECKING:  # pragma: no cover - hinted imports only
    import pyarrow as pa

__all__ = [
    "to_arrow_table",
    "to_ipc_bytes",
    "write_ipc_file",
    "read_ipc_file",
    "from_ipc_bytes",
    "iter_record_batches",
    "prepare_for_bcp",
]

PathLike = Union[str, Path]


class _DependencyError(ImportError):
    """Raised when optional dependencies are unavailable."""


def _require_polars():
    try:
        import polars as pl  # type: ignore
    except ModuleNotFoundError as exc:  # pragma: no cover - import error branch
        raise _DependencyError(
            "polars is required for arrow_bridge helpers. Install with 'pip install polars'."
        ) from exc
    return pl


def _require_pyarrow():
    try:
        import pyarrow as pa  # type: ignore
        import pyarrow.ipc as ipc  # type: ignore
    except ModuleNotFoundError as exc:  # pragma: no cover - import error branch
        raise _DependencyError(
            "pyarrow is required for arrow_bridge helpers. Install with 'pip install pyarrow'."
        ) from exc
    return pa, ipc


def to_arrow_table(df) -> "pa.Table":
    """Return a ``pyarrow.Table`` representation of *df*.

    Parameters
    ----------
    df:
        Either a Polars ``DataFrame``/``LazyFrame`` or an existing ``pyarrow.Table``.

    Returns
    -------
    pa.Table
        The zero-copy Arrow table when possible (Polars shares buffers with
        PyArrow for primitive/string columns).
    """

    pa, _ = _require_pyarrow()

    if isinstance(df, pa.Table):
        return df

    pl = _require_polars()
    if isinstance(df, pl.LazyFrame):
        df = df.collect()
    if isinstance(df, pl.DataFrame):
        return df.to_arrow()

    raise TypeError(
        "to_arrow_table expects a Polars DataFrame/LazyFrame or a pyarrow.Table. "
        f"Got type {type(df)!r}."
    )


def to_ipc_bytes(
    df,
    *,
    compression: Optional[str] = None,
    use_stream: bool = False,
) -> bytes:
    """Serialise *df* to Arrow IPC (Feather v2) bytes.

    Parameters
    ----------
    df
        Polars ``DataFrame``/``LazyFrame`` or ``pyarrow.Table``.
    compression
        Optional compression codec (``"lz4"`` or ``"zstd"``). ``None`` keeps the
        payload uncompressed, matching Arrow's speed-oriented guidance.
    use_stream
        When ``True`` the IPC *stream* format is generated instead of the file
        format. Streams are ideal for sockets/pipes; files are better for
        memory-mapping.
    """

    pa, ipc = _require_pyarrow()
    table = to_arrow_table(df)

    sink = pa.BufferOutputStream()
    if use_stream:
        with ipc.new_stream(sink, table.schema, options=_ipc_options(compression)) as writer:
            writer.write_table(table)
    else:
        with ipc.new_file(sink, table.schema, options=_ipc_options(compression)) as writer:
            writer.write_table(table)
    return sink.getvalue().to_pybytes()


def write_ipc_file(
    df,
    destination: PathLike,
    *,
    compression: Optional[str] = None,
    use_stream: bool = False,
) -> Path:
    """Write *df* to an Arrow IPC (Feather v2) file.

    Parameters
    ----------
    destination
        Path to write. When ``use_stream`` is ``True`` a ``*.arrow_stream`` file
        is conventionally used; otherwise ``*.arrow``/``*.feather``.
    """

    path = Path(destination)
    data = to_ipc_bytes(df, compression=compression, use_stream=use_stream)
    path.write_bytes(data)
    return path


def read_ipc_file(
    source: PathLike,
    *,
    memory_map: bool = True,
) -> "pa.Table":
    """Read an Arrow IPC *file* into a ``pyarrow.Table``.

    Parameters
    ----------
    memory_map
        When ``True`` the file is memory-mapped for zero-copy reads (Arrow maps
        record batch buffers directly). Falls back to regular reads when memory
        mapping is not possible.
    """

    pa, ipc = _require_pyarrow()
    path = Path(source)
    if memory_map:
        try:
            mmap = pa.memory_map(path.as_posix())
            reader = ipc.RecordBatchFileReader(mmap)
            return reader.read_all()
        except (pa.ArrowInvalid, FileNotFoundError):
            # Fall back to buffered open below
            pass
    with path.open("rb") as fh:
        reader = ipc.RecordBatchFileReader(fh)
        return reader.read_all()


def from_ipc_bytes(
    payload: Union[bytes, bytearray, memoryview],
    *,
    use_stream: bool = False,
) -> "pa.Table":
    """Load an Arrow table from IPC bytes produced by :func:`to_ipc_bytes`."""

    pa, ipc = _require_pyarrow()
    buffer_reader = pa.BufferReader(payload)
    if use_stream:
        reader = ipc.RecordBatchStreamReader(buffer_reader)
    else:
        reader = ipc.RecordBatchFileReader(buffer_reader)
    return reader.read_all()


def iter_record_batches(
    source: Union["pa.Table", PathLike, bytes, bytearray, memoryview],
    *,
    use_stream: Optional[bool] = None,
) -> Iterable["pa.RecordBatch"]:
    """Yield ``pyarrow.RecordBatch`` objects from ``source``.

    This utility supports both IPC files (default) and streams (set
    ``use_stream`` to ``True``). ``source`` may be a ``pyarrow.Table``, a path,
    or raw bytes.
    """

    pa, ipc = _require_pyarrow()

    if isinstance(source, pa.Table):
        yield from source.to_batches()
        return

    if isinstance(source, (bytes, bytearray, memoryview)):
        reader_source = pa.BufferReader(source)
        stream_mode = bool(use_stream)
    else:
        path = Path(source)
        reader_source = path.open("rb")
        stream_mode = bool(use_stream)

    try:
        if stream_mode:
            reader = ipc.RecordBatchStreamReader(reader_source)
        else:
            reader = ipc.RecordBatchFileReader(reader_source)
        for batch_index in range(reader.num_record_batches):
            yield reader.get_batch(batch_index)
    finally:
        # ``BufferReader`` exposes ``close``; ``open`` objects should be closed.
        try:  # pragma: no cover - context manager handles normal case
            reader_source.close()  # type: ignore[attr-defined]
        except AttributeError:
            pass


def _ipc_options(compression: Optional[str]):
    _, ipc = _require_pyarrow()
    if compression is None:
        return ipc.IpcWriteOptions(compression=None)
    compression = compression.lower()
    if compression not in {"lz4", "zstd"}:
        raise ValueError("Unsupported compression codec. Use 'lz4', 'zstd', or None.")
    return ipc.IpcWriteOptions(compression=compression)


def prepare_for_bcp(df, *, compression: Optional[str] = None) -> bytes:
    """Prepare a Polars DataFrame for high-performance BCP bulk insert.

    This is a convenience wrapper around :func:`to_ipc_bytes` that produces
    Arrow IPC bytes suitable for the C++ BCP (bulk copy program) path in
    :meth:`pygim.mssql_strategy.MssqlStrategyNative.bulk_insert_arrow_bcp`.

    Parameters
    ----------
    df
        Polars ``DataFrame`` or ``pyarrow.Table`` to serialize.
    compression
        Optional compression (``None`` recommended for BCP speed).

    Returns
    -------
    bytes
        Arrow IPC file format bytes that can be passed directly to
        ``bulk_insert_arrow_bcp``.

    Examples
    --------
    >>> import polars as pl
    >>> from pygim.arrow_bridge import prepare_for_bcp
    >>> df = pl.DataFrame({"id": [1, 2], "value": [3.14, 2.72]})
    >>> arrow_bytes = prepare_for_bcp(df)
    >>> # strategy.bulk_insert_arrow_bcp("my_table", arrow_bytes)
    """
    return to_ipc_bytes(df, compression=compression, use_stream=False)
