# -*- coding: utf-8 -*-
"""
CLI for Python Gimmicks.
"""

from pygim.pathset import PathSet
from pygim.registry import Registry
from pygim.factory import Factory

# Import C++ extension modules explicitly
try:  # normal pybind11 extension import
	from . import _repository as _repo_mod  # type: ignore
	Repository = _repo_mod.Repository  # type: ignore[attr-defined]
	Query = _repo_mod.Query  # type: ignore[attr-defined]
	acquire_repository = _repo_mod.acquire_repository  # type: ignore[attr-defined]
	StatusPrinter = _repo_mod.StatusPrinter  # type: ignore[attr-defined]
except Exception:  # pragma: no cover - if compiled extension missing
	pass


def create_df(schema: dict, rows: int = 100_000, *, seed: int = 42,
              null_fraction: float = 0.0):
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

	Returns
	-------
	polars.DataFrame or pyarrow.Table
	    Polars DataFrame if polars is installed, else PyArrow Table.

	Examples
	--------
	>>> import pygim
	>>> df = pygim.create_df({"id": "int32", "name": "string"}, rows=10)
	"""
	from pygim.datagen import generate as _generate  # C++ extension
	exporter = _generate(schema, rows, seed, null_fraction)
	try:
		import polars as pl
		import pyarrow as pa
		reader = pa.RecordBatchReader.from_stream(exporter)
		return pl.from_arrow(reader.read_all())
	except ImportError:
		import pyarrow as pa
		return pa.RecordBatchReader.from_stream(exporter).read_all()


__all__ = ["PathSet", "Registry", "Factory", "create_df",
           "Repository", "Query", "acquire_repository", "StatusPrinter"]
