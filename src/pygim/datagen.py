# -*- coding: utf-8 -*-
"""Columns of test data, made in C++ with Arrow's builders.

``generate(schema, rows)`` makes a table of that many rows, one column per entry of the schema
(name -> type, Arrow's names or SQL's: ``supported_types()``), and hands it over as Arrow's C
stream. polars, pyarrow and DuckDB build their own table from it, with no copy, so the caller
chooses the library and pygim names none:

>>> import polars as pl
>>> from pygim import datagen
>>> users = pl.DataFrame(datagen.generate({"id": "serial", "name": "string"}, rows=3))
>>> list(users["id"])
[1, 2, 3]

The same seed makes the same data, and two seeds make two, so a test or a benchmark can name its
data by its seed.
"""

# Arrow's shared libraries load before the extension that links them, so the
# loader resolves libarrow from the pyarrow package (see pygim/persistence.py).
import pyarrow  # noqa: F401

from pygim._datagen import generate, supported_types

__all__ = ["generate", "supported_types"]
