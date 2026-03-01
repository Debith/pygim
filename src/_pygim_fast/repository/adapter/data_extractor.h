// DataExtractor: centralises ALL Python → C++ data conversion.
// This is the ONLY place in the repository subsystem that inspects py::object
// types, calls Python attributes, or handles Polars/Arrow protocol negotiation.
//
// Strategies never see py::object — DataExtractor converts everything to
// core value types before handing data to RepositoryCore / strategies.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../core/query_intent.h"
#include "../core/value_types.h"

// Forward-declare Arrow reader.
namespace arrow {
class RecordBatchReader;
} // namespace arrow

namespace pygim::adapter {

namespace py = pybind11;

class DataExtractor {
public:
    // ---- Cell-level conversion ---------------------------------------------

    /// Convert a single py::object to a CellValue.
    static core::CellValue to_cell(const py::object &obj) {
        if (obj.is_none()) return core::Null{};
        if (py::isinstance<py::bool_>(obj)) return obj.cast<bool>();
        if (py::isinstance<py::int_>(obj)) return obj.cast<int64_t>();
        if (py::isinstance<py::float_>(obj)) return obj.cast<double>();
        // Fallback: convert to string.
        return py::str(obj).cast<std::string>();
    }

    // ---- Key extraction ----------------------------------------------------

    /// Extract a TablePkKey from a Python key (expected: tuple(table, pk)).
    static core::TablePkKey extract_table_pk(const py::object &key) {
        if (!py::isinstance<py::tuple>(key)) {
            throw std::runtime_error("DataExtractor: key must be a tuple(table, pk)");
        }
        auto t = key.cast<py::tuple>();
        if (t.size() < 2) {
            throw std::runtime_error("DataExtractor: key tuple must have at least 2 elements");
        }
        core::TablePkKey result;
        result.table = t[0].cast<std::string>();
        result.pk = to_cell(py::reinterpret_borrow<py::object>(t[1]));
        return result;
    }

    // ---- Row-level extraction ----------------------------------------------

    /// Extract a RowMap from a Python dict-like object.
    static core::RowMap extract_row_map(const py::object &dict_like) {
        if (!py::isinstance<py::dict>(dict_like)) {
            throw std::runtime_error("DataExtractor: value must be a dict");
        }
        core::RowMap result;
        py::dict d = dict_like.cast<py::dict>();
        for (auto &item : d) {
            std::string col = py::str(item.first).cast<std::string>();
            result[col] = to_cell(py::reinterpret_borrow<py::object>(item.second));
        }
        return result;
    }

    // ---- QueryIntent from Python Query object ------------------------------

    /// Extract a QueryIntent from a Python object that has sql/params attributes.
    /// This handles the legacy Query objects that carry pre-rendered SQL.
    static core::QueryIntent extract_query_intent(const py::object &query_obj) {
        // If it has structured builder attributes, extract them.
        // Otherwise, treat it as a pre-rendered SQL statement.
        core::QueryIntent intent;
        if (py::hasattr(query_obj, "table") && py::hasattr(query_obj, "columns")) {
            // Structured query (future path — adapter-side Query builder)
            intent.table = query_obj.attr("table").cast<std::string>();
            if (py::hasattr(query_obj, "columns")) {
                py::object cols = query_obj.attr("columns");
                if (!cols.is_none()) {
                    for (auto h : cols) {
                        intent.columns.push_back(py::str(h).cast<std::string>());
                    }
                }
            }
        }
        return intent;
    }

    // ---- Bulk data extraction -----------------------------------------------

    /// Detect if a Python object is a Polars DataFrame.
    static bool is_polars_dataframe(const py::object &obj) {
        try {
            py::object cls = obj.attr("__class__");
            std::string mod = py::str(cls.attr("__module__")).cast<std::string>();
            return mod.find("polars") != std::string::npos && py::hasattr(obj, "get_column");
        } catch (...) {
            return false;
        }
    }

    /// Extract a TypedColumnBatch from a Polars DataFrame.
    static core::TypedColumnBatch extract_batch_from_polars(const py::object &df,
                                                            const std::vector<std::string> &columns) {
        core::TypedColumnBatch batch;
        batch.column_names = columns;
        batch.row_count = df.attr("height").cast<size_t>();
        batch.columns.reserve(columns.size());

        for (const auto &col_name : columns) {
            py::object series = df.attr("get_column")(col_name);
            std::string dtype = py::str(series.attr("dtype")).cast<std::string>();
            batch.columns.push_back(extract_polars_column(series, dtype, batch.row_count));
        }
        return batch;
    }

    /// Extract a TypedColumnBatch from a Python iterable of sequences.
    static core::TypedColumnBatch extract_batch_from_iterable(const py::object &rows,
                                                              const std::vector<std::string> &columns) {
        core::TypedColumnBatch batch;
        batch.column_names = columns;
        const size_t ncols = columns.size();

        // First pass: collect all rows as CellValues.
        std::vector<std::vector<core::CellValue>> row_data;
        for (py::handle h : rows) {
            py::object row_obj = py::reinterpret_borrow<py::object>(h);
            if (!py::isinstance<py::sequence>(row_obj)) {
                throw std::runtime_error("DataExtractor: each row must be a sequence");
            }
            py::sequence seq = row_obj.cast<py::sequence>();
            if (static_cast<size_t>(py::len(seq)) != ncols) {
                throw std::runtime_error("DataExtractor: row length mismatch");
            }
            std::vector<core::CellValue> row;
            row.reserve(ncols);
            for (size_t c = 0; c < ncols; ++c) {
                row.push_back(to_cell(seq[static_cast<py::ssize_t>(c)]));
            }
            row_data.push_back(std::move(row));
        }
        batch.row_count = row_data.size();

        // Second pass: transpose row-major → column-major typed storage.
        batch.columns.resize(ncols);
        for (size_t c = 0; c < ncols; ++c) {
            auto &col = batch.columns[c];
            // Infer type from first non-null value.
            col.kind = infer_column_kind(row_data, c);
            fill_column(col, row_data, c, batch.row_count);
        }
        return batch;
    }

    // ---- Result set → Python conversion ------------------------------------

    /// Convert a ResultSet to a Python list of dicts.
    static py::list result_set_to_py(const core::ResultSet &rs) {
        py::list rows;
        for (const auto &row : rs.rows) {
            py::dict d;
            for (size_t c = 0; c < rs.columns.size() && c < row.size(); ++c) {
                d[py::str(rs.columns[c])] = cell_to_py(row[c]);
            }
            rows.append(std::move(d));
        }
        return rows;
    }

    /// Convert a CellValue to a py::object.
    static py::object cell_to_py(const core::CellValue &v) {
        struct Visitor {
            py::object operator()(core::Null) const { return py::none(); }
            py::object operator()(bool b) const { return py::bool_(b); }
            py::object operator()(int64_t i) const { return py::int_(i); }
            py::object operator()(double d) const { return py::float_(d); }
            py::object operator()(const std::string &s) const { return py::str(s); }
        };
        return std::visit(Visitor{}, v);
    }

    /// Convert a RowMap to a py::dict.
    static py::dict row_map_to_py(const core::RowMap &row) {
        py::dict d;
        for (const auto &[k, v] : row) {
            d[py::str(k)] = cell_to_py(v);
        }
        return d;
    }

private:
    // ---- Polars column extraction helpers -----------------------------------

    static core::TypedColumnBatch::Column extract_polars_column(const py::object &series,
                                                                const std::string &dtype,
                                                                size_t total_rows) {
        core::TypedColumnBatch::Column col;
        // NOTE: dtype strings like "UInt8", "Int32", "Float32" all contain the
        // substring we match on, but the underlying numpy array may have a
        // narrower element width.  Always widen to the canonical width via
        // numpy astype (no-copy when already correct) to avoid buffer overreads.
        py::object np = py::module_::import("numpy");
        if (dtype.find("Int") != std::string::npos) {
            col.kind = core::TypedColumnBatch::Kind::I64;
            py::object np_arr = series.attr("to_numpy")();
            np_arr = np_arr.attr("astype")(np.attr("int64"), py::arg("copy") = false);
            auto *ptr = reinterpret_cast<const int64_t *>(
                np_arr.attr("ctypes").attr("data").cast<intptr_t>());
            col.i64_data.assign(ptr, ptr + total_rows);
        } else if (dtype.find("Float") != std::string::npos) {
            col.kind = core::TypedColumnBatch::Kind::F64;
            py::object np_arr = series.attr("to_numpy")();
            np_arr = np_arr.attr("astype")(np.attr("float64"), py::arg("copy") = false);
            auto *ptr = reinterpret_cast<const double *>(
                np_arr.attr("ctypes").attr("data").cast<intptr_t>());
            col.f64_data.assign(ptr, ptr + total_rows);
        } else if (dtype.find("Boolean") != std::string::npos) {
            col.kind = core::TypedColumnBatch::Kind::BOOL;
            py::object np_arr = series.attr("to_numpy")();
            auto *ptr = reinterpret_cast<const uint8_t *>(
                np_arr.attr("ctypes").attr("data").cast<intptr_t>());
            col.bool_data.assign(ptr, ptr + total_rows);
        } else {
            col.kind = core::TypedColumnBatch::Kind::STR;
            py::list lst = series.attr("to_list")().cast<py::list>();
            col.str_data.reserve(total_rows);
            for (size_t i = 0; i < total_rows; ++i) {
                py::handle h = lst[static_cast<py::ssize_t>(i)];
                if (h.is_none()) {
                    col.str_data.emplace_back();
                    if (col.null_mask.empty()) col.null_mask.resize(total_rows, false);
                    col.null_mask[i] = true;
                } else {
                    col.str_data.push_back(py::str(h).cast<std::string>());
                }
            }
        }
        return col;
    }

    // ---- Iterable column inference helpers ----------------------------------

    static core::TypedColumnBatch::Kind infer_column_kind(
        const std::vector<std::vector<core::CellValue>> &rows, size_t col_idx) {
        for (const auto &row : rows) {
            const auto &v = row[col_idx];
            if (std::holds_alternative<core::Null>(v)) continue;
            if (std::holds_alternative<bool>(v)) return core::TypedColumnBatch::Kind::BOOL;
            if (std::holds_alternative<int64_t>(v)) return core::TypedColumnBatch::Kind::I64;
            if (std::holds_alternative<double>(v)) return core::TypedColumnBatch::Kind::F64;
            if (std::holds_alternative<std::string>(v)) return core::TypedColumnBatch::Kind::STR;
        }
        return core::TypedColumnBatch::Kind::STR; // default for all-null columns
    }

    static void fill_column(core::TypedColumnBatch::Column &col,
                            const std::vector<std::vector<core::CellValue>> &rows,
                            size_t col_idx, size_t row_count) {
        bool has_any_null = false;
        for (size_t r = 0; r < row_count; ++r) {
            if (std::holds_alternative<core::Null>(rows[r][col_idx])) {
                has_any_null = true;
                break;
            }
        }
        if (has_any_null) {
            col.null_mask.resize(row_count, false);
        }

        switch (col.kind) {
        case core::TypedColumnBatch::Kind::I64:
            col.i64_data.reserve(row_count);
            for (size_t r = 0; r < row_count; ++r) {
                const auto &v = rows[r][col_idx];
                if (std::holds_alternative<core::Null>(v)) {
                    col.i64_data.push_back(0);
                    col.null_mask[r] = true;
                } else {
                    col.i64_data.push_back(std::get<int64_t>(v));
                }
            }
            break;
        case core::TypedColumnBatch::Kind::F64:
            col.f64_data.reserve(row_count);
            for (size_t r = 0; r < row_count; ++r) {
                const auto &v = rows[r][col_idx];
                if (std::holds_alternative<core::Null>(v)) {
                    col.f64_data.push_back(0.0);
                    col.null_mask[r] = true;
                } else {
                    col.f64_data.push_back(std::get<double>(v));
                }
            }
            break;
        case core::TypedColumnBatch::Kind::BOOL:
            col.bool_data.reserve(row_count);
            for (size_t r = 0; r < row_count; ++r) {
                const auto &v = rows[r][col_idx];
                if (std::holds_alternative<core::Null>(v)) {
                    col.bool_data.push_back(0);
                    col.null_mask[r] = true;
                } else {
                    col.bool_data.push_back(std::get<bool>(v) ? 1 : 0);
                }
            }
            break;
        case core::TypedColumnBatch::Kind::STR:
            col.str_data.reserve(row_count);
            for (size_t r = 0; r < row_count; ++r) {
                const auto &v = rows[r][col_idx];
                if (std::holds_alternative<core::Null>(v)) {
                    col.str_data.emplace_back();
                    if (col.null_mask.empty()) col.null_mask.resize(row_count, false);
                    col.null_mask[r] = true;
                } else {
                    col.str_data.push_back(std::get<std::string>(v));
                }
            }
            break;
        }
    }
};

} // namespace pygim::adapter
