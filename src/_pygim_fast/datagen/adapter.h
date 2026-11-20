#pragma once

#include "core.h"
#include <pybind11/pybind11.h>
#include "../errors/adapter.h"

namespace py = pybind11;

namespace pygim::datagen {

// ── Arrow PyCapsule protocol export ─────────────────────────────────────────
// Returns an object implementing __arrow_c_stream__, compatible with
// pyarrow.RecordBatchReader.from_stream() and polars.from_arrow().

struct ArrowStreamExporter {
    std::shared_ptr<arrow::RecordBatch> batch;

    /// PyCapsule protocol: called by pyarrow / polars to consume the data.
    py::capsule arrow_c_stream(py::object /*requested_schema*/ = py::none()) const {
        auto reader = arrow_bridge::value_of(arrow::RecordBatchReader::Make({batch}, batch->schema()), "reading the generated batch");
        arrow_bridge::exported_stream stream(std::move(reader));
        return py::capsule(static_cast<void*>(stream.release()), "arrow_array_stream",
                           [](void* ptr) { arrow_bridge::exported_stream::free(static_cast<ArrowArrayStream*>(ptr)); });
    }
};

/// Top-level entry point called from Python: the schema read into column specs, then the batch
/// generated with the GIL released, since it touches nothing but its own columns.
inline ArrowStreamExporter generate(
    const py::dict& schema_dict,
    int64_t rows,
    uint64_t seed = 42,
    double null_fraction = 0.0)
{
    std::vector<ColumnSpec> columns;
    columns.reserve(py::len(schema_dict));
    for (auto item : schema_dict) {
        columns.push_back({
            item.first.cast<std::string>(),
            parse_type(item.second.cast<std::string>()),
        });
    }

    std::shared_ptr<arrow::RecordBatch> batch;
    {
        py::gil_scoped_release nogil;
        batch = generate_batch(columns, rows, seed, null_fraction);
    }
    return ArrowStreamExporter{std::move(batch)};
}

} // namespace pygim::datagen
