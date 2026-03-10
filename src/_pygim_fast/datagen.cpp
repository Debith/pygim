// datagen.cpp – pybind11 module definition for fast test-data generation.
//
// Exposes pygim.datagen.generate() which returns an ArrowStreamExporter
// implementing __arrow_c_stream__ (Arrow PyCapsule protocol).  Consumed by
// pyarrow.RecordBatchReader.from_stream() or polars.from_arrow().

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "datagen.h"

namespace py = pybind11;

using namespace pygim::datagen;

PYBIND11_MODULE(datagen, m) {
    m.doc() = "Fast test-data generator backed by Arrow array builders.\n"
              "Generates columnar Arrow data entirely in C++ — typically\n"
              "50–100× faster than equivalent pure-Python generation.";

    // ── ArrowStreamExporter (PyCapsule protocol) ────────────────────────
    py::class_<ArrowStreamExporter>(m, "ArrowStreamExporter",
        "Wrapper implementing the Arrow PyCapsule protocol (__arrow_c_stream__).\n"
        "Pass directly to pyarrow.RecordBatchReader.from_stream() or pl.from_arrow().")
        .def("__arrow_c_stream__", &ArrowStreamExporter::arrow_c_stream,
             py::arg("requested_schema") = py::none(),
             "Export as Arrow C Data Interface stream (PyCapsule protocol).");

    // ── generate() ──────────────────────────────────────────────────────
    m.def("generate", &generate,
          py::arg("schema"),
          py::arg("rows") = 100'000,
          py::arg("seed") = 42,
          py::arg("null_fraction") = 0.0,
          R"doc(
Generate test data as an Arrow stream.

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
    Number of rows to generate (default: 100,000).
seed : int
    PRNG seed for deterministic generation (default: 42).
null_fraction : float
    Fraction of NULL values per column in [0.0, 1.0] (default: 0.0).

Returns
-------
ArrowStreamExporter
    Object implementing ``__arrow_c_stream__``.  Pass to
    ``pyarrow.RecordBatchReader.from_stream()`` or ``polars.from_arrow()``.

Examples
--------
>>> from pygim.datagen import generate
>>> exporter = generate({"id": "int32", "name": "string"}, rows=1000)
>>> import pyarrow as pa
>>> table = pa.RecordBatchReader.from_stream(exporter).read_all()
>>> table.num_rows
1000
)doc");

    // ── supported_types() ───────────────────────────────────────────────
    m.def("supported_types", &supported_types,
          "Return a list of all supported column type names.");
}
