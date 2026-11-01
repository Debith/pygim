// datagen/bindings.cpp – pybind11 module definition for fast test-data generation: the compiled
// half of pygim.datagen (src/pygim/datagen.py loads Arrow first, then this).
//
// generate() returns an ArrowStreamExporter implementing __arrow_c_stream__ (Arrow PyCapsule
// protocol): polars, pyarrow and DuckDB build their own tables from it (D95).

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "adapter.h"

namespace py = pybind11;

using namespace pygim::datagen;

PYBIND11_MODULE(_datagen, m) {
    m.doc() = "Fast test-data generator backed by Arrow array builders.\n"
              "Generates columnar Arrow data entirely in C++ — typically\n"
              "50–100× faster than equivalent pure-Python generation.";

    // ── ArrowStreamExporter (PyCapsule protocol) ────────────────────────
    pygim::arrow_stream::bind(m, "ArrowStreamExporter",
        "Wrapper implementing the Arrow PyCapsule protocol (__arrow_c_stream__).\n"
        "Build a table from it: polars.DataFrame(it), pyarrow.table(it), DuckDB's from_arrow(it);\n"
        "each reads a fresh stream, so one can be read more than once.");

    // ── generate() ──────────────────────────────────────────────────────
    m.def("generate", &generate,
          py::arg("schema"),
          py::arg("rows") = 100'000,
          py::kw_only(),
          py::arg("seed") = 42,
          py::arg("null_fraction") = 0.0,
          R"doc(
Columns of test data as an Arrow stream: each column of the schema, `rows` long, made in C++.

Parameters
----------
schema : Mapping[str, str]
    Column name -> type name: Arrow's (int8..int64, uint8..uint64, bool, float32, float64,
    string, date, time, timestamp, duration, binary, uuid) or SQL's (tinyint, smallint, bigint,
    bit, real, double, nvarchar, varchar, datetime, datetime2, varbinary, uniqueidentifier);
    serial counts 1, 2, 3, ... for a key. supported_types() lists them.
rows : int
    How many rows (default: 100,000).
seed : int
    Names the data: the same seed makes the same data, two seeds two (default: 42).
null_fraction : float
    Each value's chance of being null, in [0.0, 1.0] (default: 0.0).

Returns
-------
ArrowStreamExporter
    Implements ``__arrow_c_stream__``: build a table from it with ``polars.DataFrame(data)``,
    ``pyarrow.table(data)`` or DuckDB's ``from_arrow(data)``, with no copy. Each reads a fresh
    stream, so the same object can be read more than once.

Examples
--------
>>> import polars as pl
>>> from pygim import datagen
>>> pl.DataFrame(datagen.generate({"id": "serial", "name": "string"}, rows=1000)).shape
(1000, 2)
)doc");

    // ── supported_types() ───────────────────────────────────────────────
    m.def("supported_types", &supported_types,
          "Return a list of all supported column type names.");
}
