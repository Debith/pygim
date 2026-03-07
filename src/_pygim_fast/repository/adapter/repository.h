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
#include "extraction_policy.h"
#include "query_adapter.h"

#include "../mssql_strategy/mssql_strategy.h"

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

    // Non-copyable (RepositoryCore owns unique_ptr<Strategy>); movable.
    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;
    Repository(Repository &&) = default;
    Repository &operator=(Repository &&) = default;

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
        // Skip Arrow path — caller explicitly provides column schema.
        auto view = ExtractionPolicy::extract(rows, columns, /*prefer_arrow=*/false);
        core::TableSpec ts{table, columns, std::nullopt, table_hint};
        core::PersistOptions opts{core::PersistMode::Insert, batch_size, std::nullopt};
        m_core.persist(ts, std::move(view), opts);
    }

    void bulk_upsert(const std::string &table,
                     const std::vector<std::string> &columns,
                     const py::object &rows,
                     const std::string &key_column = "id",
                     int batch_size = 500,
                     const std::string &table_hint = "TABLOCK") {
        // Skip Arrow path — caller explicitly provides column schema.
        auto view = ExtractionPolicy::extract(rows, columns, /*prefer_arrow=*/false);
        core::TableSpec ts{table, columns, key_column, table_hint};
        core::PersistOptions opts{core::PersistMode::Upsert, batch_size, key_column};
        m_core.persist(ts, std::move(view), opts);
    }

    // ---- DataFrame persistence (Arrow path) ---------------------------------

    py::dict persist_dataframe(const std::string &table,
                               const py::object &data_frame,
                               const std::string &key_column = "id",
                               bool prefer_arrow = true,
                               const std::string &table_hint = "TABLOCK",
                               int batch_size = 1000,
                               int bcp_batch_size = 0) {
        // Columns inferred from data_frame inside ExtractionPolicy.
        auto view = ExtractionPolicy::extract(data_frame, {}, prefer_arrow);

        // Capture mode label and persist options before moving the view.
        const bool is_arrow = std::holds_alternative<core::ArrowView>(view);
        const std::string mode_str = is_arrow ? "arrow_c_stream" : "bulk_upsert";
        const core::PersistMode mode =
            is_arrow ? core::PersistMode::Insert : core::PersistMode::Upsert;

        // For BCP (ArrowView), use bcp_batch_size; 0 lets bcp_strategy use its
        // 100 000-row default, which amortizes bcp_batch() commit overhead across
        // far fewer server round-trips than the SQL-oriented batch_size=1000.
        // For MERGE (TypedBatchView), use batch_size as before.
        const int effective_batch = is_arrow ? bcp_batch_size : batch_size;

        core::TableSpec ts{table, {}, key_column, table_hint};
        core::PersistOptions opts{mode, effective_batch, key_column};
        m_core.persist(ts, std::move(view), opts);

        py::dict result;
        result["mode"] = py::str(mode_str);
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
};

} // namespace pygim::adapter
