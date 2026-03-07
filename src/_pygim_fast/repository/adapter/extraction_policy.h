// ExtractionPolicy: single, explicit point of Python → DataView conversion.
//
// This is the ONLY place in the repository subsystem that inspects py::object
// types out of single-row context. All bulk data format detection, Arrow
// protocol negotiation, and Polars handling lives here.
//
// Strategies never see py::object — ExtractionPolicy converts everything to
// a DataView (ArrowView or TypedBatchView) before handing data to the core.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>

#include "../core/value_types.h"
#include "../mssql_strategy/detail/bcp/bcp_arrow_import.h"
#include "data_extractor.h"

namespace pygim::adapter {

namespace py = pybind11;

/// Extracts a DataView from any Python bulk data object.
///
/// Dispatch order (prefer_arrow = true):
///   1a. data.__arrow_c_stream__() → zero-copy capsule.
///       Arrow C++ >= 15 (build minimum) accepts StringView ("vu") from Polars 1.x
///       end-to-end: ImportRecordBatchReader + bind_string_view both handle it.
///   1b. data._export_to_c / IPC bytes — other Arrow-capable objects
///       (e.g. PyArrow RecordBatchReader passed directly).
///   2.  Polars DataFrame               → TypedBatchView (column extraction)
///   3.  Python iterable of sequences   → TypedBatchView (row-major → column-major)
///
/// When prefer_arrow = false, step 1 is skipped entirely.
///
/// columns: expected column names for TypedBatchView paths.
///   - If empty, names are inferred from the object (DataFrame.columns etc.).
///   - Ignored for ArrowView — schema comes from the reader.
class ExtractionPolicy {
public:
    static core::DataView extract(const py::object &data,
                                  const std::vector<std::string> &columns,
                                  bool prefer_arrow = true) {
        if (prefer_arrow) {
            auto reader = try_extract_arrow_reader(data);
            if (reader) {
                return core::ArrowView{std::move(reader), "arrow_c_stream"};
            }
        }

        // TypedBatchView path: resolve column names if not provided.
        std::vector<std::string> effective_cols = columns;
        if (effective_cols.empty()) {
            effective_cols = infer_columns(data);
        }

        if (DataExtractor::is_polars_dataframe(data)) {
            return core::TypedBatchView{
                DataExtractor::extract_batch_from_polars(data, effective_cols)
            };
        }

        if (!py::hasattr(data, "__iter__")) {
            throw std::runtime_error(
                "ExtractionPolicy: data must be an Arrow/Polars object, "
                "a DataFrame, or a Python iterable");
        }
        return core::TypedBatchView{
            DataExtractor::extract_batch_from_iterable(data, effective_cols)
        };
    }

private:
    /// Try to extract an Arrow RecordBatchReader from the Python object.
    /// Returns nullptr if the object does not support any Arrow protocol.
    static std::shared_ptr<arrow::RecordBatchReader>
    try_extract_arrow_reader(const py::object &data) {
        // 1a. Zero-copy: data exports itself via the Arrow C Data Interface.
        //     Polars 1.x emits StringView ("vu"); Arrow C++ >= 15 accepts it
        //     at both ImportRecordBatchReader and bind_string_view.
        if (py::hasattr(data, "__arrow_c_stream__")) {
            try {
                py::object capsule = data.attr("__arrow_c_stream__")();
                auto result = bcp::import_arrow_reader(capsule);
                if (result.reader) return result.reader;
            } catch (...) {}
        }

        // 1b. Object with _export_to_c or IPC bytes
        //     (e.g. PyArrow RecordBatchReader passed directly).
        try {
            auto result = bcp::import_arrow_reader(data);
            if (result.reader) return result.reader;
        } catch (...) {}

        return nullptr;
    }

    /// Infer column names from the data object when not explicitly provided.
    static std::vector<std::string> infer_columns(const py::object &data) {
        std::vector<std::string> cols;
        if (py::hasattr(data, "columns")) {
            for (auto col : data.attr("columns")) {
                cols.push_back(py::cast<std::string>(col));
            }
        } else if (py::hasattr(data, "schema")) {
            py::object schema = data.attr("schema");
            if (py::hasattr(schema, "names")) {
                for (auto name : schema.attr("names")) {
                    cols.push_back(py::cast<std::string>(name));
                }
            }
        }
        return cols;
    }
};

} // namespace pygim::adapter
