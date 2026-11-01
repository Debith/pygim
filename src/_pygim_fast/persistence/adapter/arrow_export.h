// persistence/adapter/arrow_export.h
// Export Arrow Table to Python DataFrame via Arrow C Data Interface.
// Reverse of arrow_import.h — this is the load-path boundary.
//
// Must be called WITH GIL held (creates Python objects).
// Path: C++ Arrow Table → ArrowArrayStream (C Data Interface)
//       → PyArrow RecordBatchReader._import_from_c → PyArrow Table
//       → Polars/Pandas.

#pragma once

#include "../core/persistence_error.h"
#include <arrow/c/bridge.h>
#include <arrow/record_batch.h>
#include <arrow/table.h>
#include <pybind11/gil_safe_call_once.h>
#include <pybind11/pybind11.h>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace pygim::adapter {

namespace py = pybind11;

namespace detail {

// pyarrow's RecordBatchReader._import_from_c and polars.from_arrow, looked up once, at the first
// load that needs them — not on every load, as each export imported both before (D53, as
// utils/arrow_stream.h looks up pyarrow's constructors). Never destroyed: no Python object is left
// for C++'s exit handlers to release after the interpreter.
[[nodiscard]] inline py::object& pyarrow_reader_import() {
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::object> found;
    return found.call_once_and_store_result([] { return py::module_::import("pyarrow").attr("RecordBatchReader").attr("_import_from_c"); })
        .get_stored();
}
[[nodiscard]] inline py::object& polars_from_arrow() {
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::object> found;
    return found.call_once_and_store_result([] { return py::module_::import("polars").attr("from_arrow"); }).get_stored();
}

}  // namespace detail

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

    // RAII wrapper: releases the stream (if still active) then frees the struct.
    auto stream_deleter = [](ArrowArrayStream* s) {
        if (s->release) s->release(s);
        delete s;
    };
    std::unique_ptr<ArrowArrayStream, decltype(stream_deleter)> c_stream(
        new ArrowArrayStream, stream_deleter);
    std::memset(c_stream.get(), 0, sizeof(ArrowArrayStream));

    auto status = arrow::ExportRecordBatchReader(reader, c_stream.get());
    if (!status.ok()) [[unlikely]] {
        throw pygim::core::persistence_error("export_table: ExportRecordBatchReader failed: "
                                 + status.ToString());
    }

    // Import into PyArrow via C Data Interface address.
    // _import_from_c consumes the stream (sets release to nullptr).
    auto addr = py::int_(reinterpret_cast<uintptr_t>(c_stream.get()));
    auto reader_obj = detail::pyarrow_reader_import()(addr);

    // _import_from_c consumed the stream — release is now nullptr.
    // unique_ptr destructor will call delete (release guard is a no-op).
    c_stream.reset();

    auto pa_table = reader_obj.attr("read_all")();

    if (use_polars) {
        return detail::polars_from_arrow()(pa_table);
    } else {
        return pa_table.attr("to_pandas")();
    }
}

} // namespace pygim::adapter
