// Repository adapter: Python-facing class that wraps RepositoryCore.
// All py::object handling and data extraction happens HERE.
// Strategies receive and return pure C++ types — no pybind in strategy code.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../core/connection_uri.h"
#include "../core/memory_strategy.h"
#include "../core/repository_core.h"
#include "../core/strategy.h"
#include "../core/value_types.h"
#include "data_extractor.h"
#include "query_adapter.h"

#include "../mssql_strategy/detail/bcp/bcp_arrow_import.h"
#include "../mssql_strategy/mssql_strategy_v2.h"

namespace pygim::adapter {

namespace py = pybind11;

class Repository {
public:
    /// Construct a Repository from a connection URI.
    ///
    /// Supported schemes:
    ///   "memory://"              — in-memory strategy (dev / test)
    ///   "mssql://server/db"     — MSSQL via ODBC
    ///   "Driver={...};Server=…" — raw ODBC connection string (MSSQL)
    ///
    /// Optional:
    ///   transformers — enable pre-save / post-load transformer pipeline.
    explicit Repository(const std::string &connection_uri,
                        bool enable_transformers = false)
        : m_core(enable_transformers), m_uri(core::parse_uri(connection_uri))
    {
        create_strategy_from_uri();
    }

    // ---- Factory management -------------------------------------------------

    void set_factory(py::object factory) {
        m_factory_py = std::move(factory);
        m_has_factory_py = true;
    }

    void clear_factory() {
        m_factory_py = py::none();
        m_has_factory_py = false;
        m_core.clear_factory();
    }

    bool has_factory() const noexcept { return m_has_factory_py || m_core.has_factory(); }

    // ---- Transformer registration -------------------------------------------

    void add_pre_transform(py::function fn) {
        // Wrap Python callable into core PreSaveTransform.
        m_pre_transforms_py.push_back(fn);
        m_core.add_pre_transform(
            [fn](const std::string &table, const core::RowMap &data) -> core::RowMap {
                py::dict py_data = DataExtractor::row_map_to_py(data);
                py::object result = fn(py::str(table), py_data);
                return DataExtractor::extract_row_map(result);
            });
    }

    void add_post_transform(py::function fn) {
        m_post_transforms_py.push_back(fn);
        m_core.add_post_transform(
            [fn](const std::string &table, core::ResultSet rs) -> core::ResultSet {
                // Convert to Python, transform, convert back.
                py::list py_rows = DataExtractor::result_set_to_py(rs);
                py::object result = fn(py::str(table), py_rows);
                // For now, post transforms are expected to return a list of dicts.
                // Re-extract into ResultSet.
                core::ResultSet out;
                out.columns = rs.columns;
                for (py::handle row_h : result) {
                    py::dict row_d = row_h.cast<py::dict>();
                    std::vector<core::CellValue> row;
                    row.reserve(out.columns.size());
                    for (const auto &col : out.columns) {
                        if (row_d.contains(py::str(col))) {
                            row.push_back(DataExtractor::to_cell(
                                py::reinterpret_borrow<py::object>(row_d[py::str(col)])));
                        } else {
                            row.push_back(core::Null{});
                        }
                    }
                    out.rows.push_back(std::move(row));
                }
                return out;
            });
    }

    // ---- Fetch operations ---------------------------------------------------

    /// Fetch via QueryAdapter (renders using strategy's dialect).
    py::object fetch_raw(const QueryAdapter &query) {
        if (query.is_manual()) {
            auto rendered = core::RenderedQuery{query.sql(), {}};
            // Manual SQL — params are in the rendered query.
            auto rq = query.render(*m_core.find_dialect());
            auto result = m_core.fetch_raw(rq);
            if (!result.has_value()) return py::none();
            return DataExtractor::result_set_to_py(*result);
        }
        auto result = m_core.fetch_raw(query.intent());
        if (!result.has_value()) return py::none();
        return DataExtractor::result_set_to_py(*result);
    }

    /// Fetch via Python key — tries direct key-based fetch first, then query.
    py::object fetch_by_key(const py::object &key) {
        auto tpk = DataExtractor::extract_table_pk(key);

        // Fast path: direct key-based lookup (MemoryStrategy, etc.)
        auto row = m_core.fetch_by_key(tpk);
        if (row.has_value()) {
            py::dict d = DataExtractor::row_map_to_py(*row);
            if (m_has_factory_py) return m_factory_py(key, d);
            return d;
        }

        // Slow path: SQL-based fetch (requires dialect)
        const auto *dialect = m_core.find_dialect();
        if (!dialect) return py::none(); // No SQL-capable strategy

        core::QueryIntent intent;
        intent.from_table(tpk.table)
            .select({"*"})
            .where("id=?", tpk.pk);
        auto result = m_core.fetch(intent);
        if (!result.has_value() || result->empty()) return py::none();
        // Return first row as dict.
        py::dict d;
        for (size_t c = 0; c < result->columns.size(); ++c) {
            d[py::str(result->columns[c])] = DataExtractor::cell_to_py(result->rows[0][c]);
        }
        if (m_has_factory_py) return m_factory_py(key, d);
        return d;
    }

    /// Mapping-like get: fetch by key, raise if not found.
    py::object get(const py::object &key) {
        py::object result = fetch_by_key(key);
        if (result.is_none()) {
            throw std::runtime_error("Repository: key not found");
        }
        return result;
    }

    /// Mapping-like get with default.
    py::object get_default(const py::object &key, const py::object &default_value) {
        py::object result = fetch_by_key(key);
        if (result.is_none()) return default_value;
        return result;
    }

    bool contains(const py::object &key) {
        auto tpk = DataExtractor::extract_table_pk(key);
        // Fast path: direct key check
        if (m_core.contains_key(tpk)) return true;
        // Slow path: fetch and check
        py::object result = fetch_by_key(key);
        return !result.is_none();
    }

    // ---- Save operations ----------------------------------------------------

    void save(const py::object &key, const py::object &value) {
        auto tpk = DataExtractor::extract_table_pk(key);
        auto data = DataExtractor::extract_row_map(value);
        m_core.save(tpk, std::move(data));
    }

    // ---- Bulk operations ----------------------------------------------------

    void bulk_insert(const std::string &table,
                     const std::vector<std::string> &columns,
                     const py::object &rows,
                     int batch_size = 1000,
                     const std::string &table_hint = "TABLOCK") {
        auto batch = extract_bulk_batch(rows, columns);
        m_core.bulk_insert(table, batch, batch_size, table_hint);
    }

    void bulk_upsert(const std::string &table,
                     const std::vector<std::string> &columns,
                     const py::object &rows,
                     const std::string &key_column = "id",
                     int batch_size = 500,
                     const std::string &table_hint = "TABLOCK") {
        auto batch = extract_bulk_batch(rows, columns);
        m_core.bulk_upsert(table, batch, key_column, batch_size, table_hint);
    }

    // ---- DataFrame persistence (Arrow path) ---------------------------------

    py::dict persist_dataframe(const std::string &table,
                               const py::object &data_frame,
                               const std::string &key_column = "id",
                               bool prefer_arrow = true,
                               const std::string &table_hint = "TABLOCK",
                               int batch_size = 1000) {
        // Arrow extraction is adapter-only logic.
        // Attempt Arrow C stream first, then IPC, then fallback to bulk_upsert.
        if (prefer_arrow) {
            try {
                auto reader = extract_arrow_reader(data_frame);
                if (reader) {
                    m_core.persist_arrow(table, std::move(reader), "arrow_c_stream",
                                         batch_size, table_hint);
                    py::dict result;
                    result["mode"] = py::str("arrow_c_stream");
                    result["success"] = py::bool_(true);
                    return result;
                }
            } catch (...) {
                // Fall through to bulk path.
            }
        }

        // Fallback: extract columns and use bulk_upsert.
        std::vector<std::string> columns;
        for (auto item : data_frame.attr("columns")) {
            columns.push_back(py::cast<std::string>(item));
        }
        auto batch = extract_bulk_batch(data_frame, columns);
        m_core.bulk_upsert(table, batch, key_column, batch_size, table_hint);
        py::dict result;
        result["mode"] = py::str("bulk_upsert");
        result["success"] = py::bool_(true);
        return result;
    }

    // ---- Introspection ------------------------------------------------------

    size_t strategy_count() const { return m_core.strategy_count(); }
    bool transformers_enabled() const { return m_core.transformers_enabled(); }
    const core::ConnectionUri &uri() const noexcept { return m_uri; }

    std::string repr() const {
        return "Repository(scheme=\"" + m_uri.scheme + "\""
               ", transformers=" + (m_core.transformers_enabled() ? "True" : "False") +
               ", factory=" + (has_factory() ? "True" : "False") + ")";
    }

private:
    core::RepositoryCore m_core;
    core::ConnectionUri m_uri;
    py::object m_factory_py = py::none();
    bool m_has_factory_py{false};
    std::vector<py::function> m_pre_transforms_py;
    std::vector<py::function> m_post_transforms_py;

    /// Create the single strategy from the parsed URI.
    void create_strategy_from_uri() {
        if (m_uri.scheme == "memory") {
            m_core.add_strategy(std::make_unique<core::MemoryStrategy>());
        } else if (m_uri.scheme == "mssql") {
            auto conn_str = core::build_odbc_connection_string(m_uri);
            m_core.add_strategy(std::make_unique<mssql::MssqlStrategy>(conn_str));
        } else {
            throw std::invalid_argument(
                "Repository: unsupported scheme '" + m_uri.scheme + "'. "
                "Supported: memory://, mssql://server/db");
        }
    }

    // ---- Internal helpers ---------------------------------------------------

    core::TypedColumnBatch extract_bulk_batch(const py::object &rows,
                                              const std::vector<std::string> &columns) {
        if (DataExtractor::is_polars_dataframe(rows)) {
            return DataExtractor::extract_batch_from_polars(rows, columns);
        }
        if (!py::hasattr(rows, "__iter__")) {
            throw std::runtime_error("Repository: rows must be an iterable or a Polars DataFrame");
        }
        return DataExtractor::extract_batch_from_iterable(rows, columns);
    }

    /// Extract Arrow RecordBatchReader from a Python DataFrame.
    /// Uses bcp_arrow_import.h for C-stream / IPC extraction.
    std::shared_ptr<arrow::RecordBatchReader> extract_arrow_reader(const py::object &data_frame) {
        // Try capsule first (Polars compat level → Arrow table → __arrow_c_stream__).
        py::object capsule = extract_arrow_capsule(data_frame);
        if (!capsule.is_none()) {
            auto result = bcp::import_arrow_reader(capsule);
            return result.reader;
        }
        // Fallback: pass the dataframe directly (supports _export_to_c / IPC).
        try {
            auto result = bcp::import_arrow_reader(data_frame);
            return result.reader;
        } catch (...) {
            return nullptr;
        }
    }

    /// Extract Arrow C stream capsule from a Python DataFrame.
    py::object extract_arrow_capsule(const py::object &data_frame) {
        try {
            // Try Polars compat level.
            py::object compat_level = try_polars_compat_oldest();
            if (!compat_level.is_none() && py::hasattr(data_frame, "to_arrow")) {
                py::object arrow_table = data_frame.attr("to_arrow")(
                    py::arg("compat_level") = compat_level);
                if (py::hasattr(arrow_table, "__arrow_c_stream__")) {
                    return arrow_table.attr("__arrow_c_stream__")();
                }
                if (py::hasattr(arrow_table, "to_reader")) {
                    return arrow_table.attr("to_reader")();
                }
                return data_frame.attr("__arrow_c_stream__")();
            }
            if (py::hasattr(data_frame, "__arrow_c_stream__")) {
                return data_frame.attr("__arrow_c_stream__")();
            }
        } catch (...) {
            // Fall through.
        }
        return py::none();
    }

    static py::object try_polars_compat_oldest() {
        try {
            py::module_ pl = py::module_::import("polars");
            return pl.attr("CompatLevel").attr("oldest")();
        } catch (...) {
            return py::none();
        }
    }
};

} // namespace pygim::adapter
