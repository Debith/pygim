// persistence/adapter/arrow_import.h
// Converts a Python Arrow-compatible object to shared_ptr<arrow::Table>.
//
// This is the ONLY file in the pipeline that uses both pybind11 AND Arrow C++.
// Supports two protocols:
//   1. PyCapsule (__arrow_c_stream__) — Polars 0.20+, PyArrow 14+
//   2. PyArrow RecordBatchReader._export_to_c — legacy fallback
// Also handles Polars DataFrame via to_arrow() → recursion.

#pragma once

#include <arrow/c/bridge.h>
#include <arrow/record_batch.h>
#include <arrow/table.h>
#include <pybind11/pybind11.h>
#include <memory>
#include <stdexcept>

namespace pygim::adapter {

namespace py = pybind11;

/// Import a Python Arrow-compatible object as a RecordBatchReader.
/// Must be called WITH GIL held (accesses Python objects throughout).
/// Supports PyCapsule protocol (__arrow_c_stream__) and PyArrow's _export_to_c.
/// @param depth  Internal recursion guard (max 1 level for to_arrow() conversion).
[[nodiscard]]
inline std::shared_ptr<arrow::RecordBatchReader>
import_record_batch_reader(py::object obj, int depth = 0) {
    // 1. PyCapsule protocol (ArrowArrayStream) — modern standard
    if (py::hasattr(obj, "__arrow_c_stream__")) {
        auto capsule = obj.attr("__arrow_c_stream__")();
        auto* c_stream = reinterpret_cast<ArrowArrayStream*>(
            PyCapsule_GetPointer(capsule.ptr(), "arrow_array_stream"));
        if (!c_stream)
            throw std::runtime_error("Arrow PyCapsule returned null stream pointer");

        auto result = arrow::ImportRecordBatchReader(c_stream);
        if (!result.ok())
            throw std::runtime_error("Failed to import Arrow stream: " + result.status().ToString());
        return *result;
    }

    // 2. PyArrow RecordBatchReader._export_to_c — legacy fallback
    if (py::hasattr(obj, "_export_to_c")) {
        ArrowArrayStream c_stream{};
        auto addr = reinterpret_cast<uintptr_t>(&c_stream);
        obj.attr("_export_to_c")(addr);

        auto result = arrow::ImportRecordBatchReader(&c_stream);
        if (!result.ok())
            throw std::runtime_error("Failed to import via _export_to_c: " + result.status().ToString());
        return *result;
    }

    // 3. Polars DataFrame — convert via to_arrow() (max 1 level of recursion)
    if (depth == 0 && py::hasattr(obj, "to_arrow")) {
        auto arrow_obj = obj.attr("to_arrow")();
        return import_record_batch_reader(std::move(arrow_obj), depth + 1);
    }

    throw py::type_error(
        "Expected an Arrow-compatible object (Polars DataFrame, PyArrow Table/RecordBatchReader) "
        "with __arrow_c_stream__ or _export_to_c protocol");
}

/// Import a Python Arrow-compatible object as a materialized arrow::Table.
/// Drains the RecordBatchReader (GIL held) and returns a fully in-memory Table.
/// This is the primary entry point for the save pipeline — the data is already in
/// memory on the Python side, so materializing avoids a false streaming abstraction.
[[nodiscard]]
inline std::shared_ptr<arrow::Table>
import_table(py::object obj) {
    auto reader = import_record_batch_reader(std::move(obj));
    auto result = reader->ToTable();
    if (!result.ok())
        throw std::runtime_error("Failed to materialize Arrow Table: " + result.status().ToString());
    return *result;
}

} // namespace pygim::adapter
