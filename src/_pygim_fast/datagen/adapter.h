#pragma once

#include "core.h"
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace pygim::datagen {

// ── Arrow PyCapsule protocol export ─────────────────────────────────────────
// Returns an object implementing __arrow_c_stream__, compatible with
// pyarrow.RecordBatchReader.from_stream() and polars.from_arrow().

struct ArrowStreamExporter {
    std::shared_ptr<arrow::RecordBatch> batch;

    /// PyCapsule protocol: called by pyarrow / polars to consume the data.
    py::capsule arrow_c_stream(py::object /*requested_schema*/ = py::none()) const {
        auto reader_result = arrow::RecordBatchReader::Make({batch}, batch->schema());
        if (!reader_result.ok())
            throw std::runtime_error(reader_result.status().ToString());

        auto* c_stream = new ArrowArrayStream;
        std::memset(c_stream, 0, sizeof(*c_stream));

        auto status = arrow::ExportRecordBatchReader(
            std::move(*reader_result), c_stream);
        if (!status.ok()) {
            delete c_stream;
            throw std::runtime_error(status.ToString());
        }

        return py::capsule(
            static_cast<void*>(c_stream), "arrow_array_stream",
            [](void* ptr) {
                auto* s = static_cast<ArrowArrayStream*>(ptr);
                if (s->release) s->release(s);
                delete s;
            });
    }
};

/// Top-level entry point called from Python.
inline ArrowStreamExporter generate(
    const py::dict& schema_dict,
    int64_t rows,
    uint64_t seed = 42,
    double null_fraction = 0.0)
{
    if (rows <= 0)
        throw std::invalid_argument("rows must be positive");
    if (null_fraction < 0.0 || null_fraction > 1.0)
        throw std::invalid_argument("null_fraction must be in [0.0, 1.0]");

    std::vector<ColumnSpec> columns;
    columns.reserve(py::len(schema_dict));

    for (auto item : schema_dict) {
        columns.push_back({
            item.first.cast<std::string>(),
            parse_type(item.second.cast<std::string>()),
        });
    }

    if (columns.empty())
        throw std::invalid_argument("schema must have at least one column");

    auto batch = generate_batch(columns, rows, seed, null_fraction);
    return ArrowStreamExporter{std::move(batch)};
}

} // namespace pygim::datagen
