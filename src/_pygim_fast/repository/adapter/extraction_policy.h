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
///   1a. data.__arrow_c_stream__() directly — zero-copy, no materialisation.
///       Fails on Arrow < 14 when Polars exports StringView ("vu" format).
///   1b. data.to_arrow(compat_level=oldest).__arrow_c_stream__() — converts
///       StringView → LargeUtf8 before export; required on Arrow < 14.
///   1c. data._export_to_c / IPC bytes — other Arrow-capable objects.
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
        //     On Arrow < 14, Polars emits StringView ("vu") which ImportRecordBatchReader
        //     rejects — the exception is caught and we fall through to 1b.
        if (py::hasattr(data, "__arrow_c_stream__")) {
            try {
                py::object capsule = data.attr("__arrow_c_stream__")();
                auto result = bcp::import_arrow_reader(capsule);
                if (result.reader) return result.reader;
            } catch (...) {}
        }

        // 1b. Compat path: materialise via to_arrow(compat_level=oldest).
        //     Polars converts StringView → LargeUtf8, which all Arrow versions handle.
        //     Required when the runtime Arrow C++ library predates StringView support.
        py::object capsule = extract_arrow_capsule_compat(data);
        if (!capsule.is_none()) {
            try {
                auto result = bcp::import_arrow_reader(capsule);
                if (result.reader) return result.reader;
            } catch (...) {}
        }

        // 1c. Object with _export_to_c or IPC bytes.
        try {
            auto result = bcp::import_arrow_reader(data);
            if (result.reader) return result.reader;
        } catch (...) {}

        return nullptr;
    }

    /// Extract a C stream capsule via to_arrow(compat_level=oldest).
    /// Converts Polars-native StringView → LargeUtf8 for Arrow < 14 compatibility.
    static py::object extract_arrow_capsule_compat(const py::object &data) {
        try {
            py::module_ pl = py::module_::import("polars");
            py::object compat_level = pl.attr("CompatLevel").attr("oldest")();
            if (py::hasattr(data, "to_arrow")) {
                py::object arrow_table = data.attr("to_arrow")(
                    py::arg("compat_level") = compat_level);
                if (py::hasattr(arrow_table, "__arrow_c_stream__")) {
                    return arrow_table.attr("__arrow_c_stream__")();
                }
                if (py::hasattr(arrow_table, "to_reader")) {
                    return arrow_table.attr("to_reader")();
                }
            }
        } catch (...) {}
        return py::none();
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
