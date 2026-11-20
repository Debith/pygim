// persistence/adapter/arrow_export.h
// Export Arrow Table to Python DataFrame via Arrow C Data Interface.
// Reverse of arrow_import.h — this is the load-path boundary.
//
// Must be called WITH GIL held (creates Python objects).
// Path: C++ Arrow Table → ArrowArrayStream (C Data Interface)
//       → PyArrow RecordBatchReader._import_from_c → PyArrow Table
//       → Polars/Pandas.

#pragma once

#include <arrow/c/bridge.h>
#include <arrow/record_batch.h>
#include <arrow/table.h>
#include <pybind11/pybind11.h>
#include <memory>
#include "../../core/arrow/bridge.h"
#include "../../errors/core.h"

namespace pygim::adapter {

namespace py = pybind11;

/// Export an Arrow Table to a Python DataFrame (Polars or Pandas).
/// Must be called WITH GIL held.
/// Uses Arrow C Data Interface address import (_import_from_c).
///
/// @param table       Materialised Arrow Table from the load pipeline.
/// @param use_polars  true → Polars DataFrame; false → Pandas DataFrame.
[[nodiscard]]
inline py::object export_table(std::shared_ptr<arrow::Table> table,
                               bool use_polars) {
    // Wrap Table in a TableBatchReader (no copy — reads existing chunks)
    auto reader = std::make_shared<arrow::TableBatchReader>(*table);

    arrow_bridge::exported_stream c_stream(reader);

    // Import into PyArrow via C Data Interface address.
    // _import_from_c consumes the stream (sets release to nullptr).
    auto pa = py::module_::import("pyarrow");
    auto addr = py::int_(reinterpret_cast<uintptr_t>(c_stream.get()));
    auto reader_obj = pa.attr("RecordBatchReader").attr("_import_from_c")(addr);

    // _import_from_c consumed the stream, so its release is null; c_stream frees only the struct.

    auto pa_table = reader_obj.attr("read_all")();

    if (use_polars) {
        return py::module_::import("polars").attr("from_arrow")(pa_table);
    } else {
        return pa_table.attr("to_pandas")();
    }
}

} // namespace pygim::adapter
