# -*- coding: utf-8 -*-
"""pygim \u2014 Lightweight, high-performance Python utilities backed by C++ extensions."""

# Top-level names resolve LAZILY (PEP 562): importing ``pygim`` — or one clean
# submodule like ``pygim.utils`` — must not drag in every compiled extension.
# A submodule that fails to load (missing build, toolchain/libstdc++ mismatch)
# then only breaks the callers that actually use it.
_LAZY_EXPORTS = {
    "PathSet": ("pygim.pathset", "PathSet"),
    "path": ("pygim.pathlike", "path"),
    "file": ("pygim.pathlike", "file"),
    "Registry": ("pygim.registry", "Registry"),
    "Factory": ("pygim.factory", "Factory"),
    "Container": ("pygim.ioc", "Container"),
    # Re-exported from the Python module (not the compiled extension directly)
    # so the top-level entry point shares its SQLAlchemy-URL translation.
    "DataStore": ("pygim.persistence", "DataStore"),
    "acquire_datastore": ("pygim.persistence", "acquire_datastore"),
}

__all__ = [*_LAZY_EXPORTS, "create_df"]


def __getattr__(name):
    try:
        module_name, attr = _LAZY_EXPORTS[name]
    except KeyError:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from None
    import importlib

    return getattr(importlib.import_module(module_name), attr)


def create_df(
    schema: dict,
    rows: int = 100_000,
    *,
    seed: int = 42,
    null_fraction: float = 0.0,
    format: str = "polars",
):
    """Generate a test DataFrame with the given schema.

    Uses a fast C++ backend with Arrow array builders — typically
    50–100× faster than equivalent pure-Python generation.

    Parameters
    ----------
    schema : dict[str, str]
        Column definitions as ``{name: type_string}``.
        Supported types: int8, int16, int32, int64, uint8–uint64, bool,
        float32, float64, string, date, time, timestamp, duration, binary, uuid,
        serial (sequential 1, 2, 3, … for PK columns).
        Also accepts SQL aliases: tinyint, smallint, bigint, bit, real, double,
        nvarchar, varchar, datetime, datetime2, varbinary, uniqueidentifier.
    rows : int
        Number of rows (default: 100,000).
    seed : int
        Deterministic PRNG seed (default: 42).
    null_fraction : float
        Fraction of NULLs per column in [0.0, 1.0] (default: 0.0).
    format : str
        Output format: ``"polars"`` (default), ``"arrow"`` (PyArrow Table).
        Use ``"arrow"`` to preserve Arrow-native types like ``fixed_size_binary``
        that Polars may convert to variable-length equivalents.

    Returns
    -------
    polars.DataFrame or pyarrow.Table
        Polars DataFrame by default, PyArrow Table when ``format="arrow"``.

    Examples
    --------
    >>> import pygim
    >>> df = pygim.create_df({"id": "int32", "name": "string"}, rows=10)  # doctest: +SKIP
    """
    import pyarrow as pa  # before the extension: it links against Arrow's libs

    from pygim.datagen import generate as _generate  # C++ extension

    exporter = _generate(schema, rows, seed, null_fraction)

    table = pa.RecordBatchReader.from_stream(exporter).read_all()
    if format == "arrow":
        return table
    import polars as pl  # hard dependency; a missing install must fail, not change the return type

    return pl.from_arrow(table)
