#pragma once

#include "core.h"
#include "../utils/arrow_stream.h"
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace pygim::datagen {

// The Arrow PyCapsule exporter is shared with every Arrow-producing extension.
using ArrowStreamExporter = pygim::arrow_stream::exporter;

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
    return ArrowStreamExporter::of(std::move(batch));
}

} // namespace pygim::datagen
