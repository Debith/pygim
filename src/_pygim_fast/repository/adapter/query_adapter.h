// QueryAdapter: Python-facing fluent query builder.
// Wraps core::QueryIntent and provides a pybind11-compatible API that
// mirrors the current Query interface for backward compatibility.
//
// Params are accepted as py::object and converted to CellValue at the
// adapter boundary — the underlying QueryIntent stores pure C++ types.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../core/query_dialect.h"
#include "../core/query_intent.h"
#include "../core/value_types.h"
#include "data_extractor.h"

namespace pygim::adapter {

namespace py = pybind11;

class QueryAdapter {
public:
    QueryAdapter() = default;

    /// Construct from pre-rendered SQL + params (legacy compatibility).
    QueryAdapter(std::string sql, std::vector<py::object> params)
        : m_manual_sql(true), m_rendered_sql(std::move(sql)) {
        m_rendered_params.reserve(params.size());
        for (auto &p : params) {
            m_rendered_params.push_back(DataExtractor::to_cell(p));
        }
    }

    // ---- Fluent builder API (mirrors current Query interface) ----------------

    QueryAdapter &select(std::vector<std::string> cols) {
        activate_builder();
        m_intent.select(std::move(cols));
        return *this;
    }

    QueryAdapter &from_table(std::string table) {
        activate_builder();
        m_intent.from_table(std::move(table));
        return *this;
    }

    QueryAdapter &where(std::string clause, py::object param) {
        activate_builder();
        m_intent.where(std::move(clause), DataExtractor::to_cell(param));
        return *this;
    }

    QueryAdapter &limit(int n) {
        activate_builder();
        m_intent.set_limit(n);
        return *this;
    }

    /// Freeze current builder state into a copy (like the old Query.build()).
    QueryAdapter build() const {
        QueryAdapter copy = *this;
        return copy;
    }

    /// Clone without rebuilding.
    QueryAdapter clone() const {
        return *this;
    }

    // ---- Access to core types -----------------------------------------------

    /// Get the underlying QueryIntent (for strategies that use intent-based dispatch).
    const core::QueryIntent &intent() const { return m_intent; }

    /// Check if this adapter holds pre-rendered manual SQL.
    bool is_manual() const noexcept { return m_manual_sql; }

    /// Render to SQL using a given dialect. If manual SQL was set, returns that.
    core::RenderedQuery render(const core::QueryDialect &dialect) const {
        if (m_manual_sql) {
            return {m_rendered_sql, m_rendered_params};
        }
        return dialect.render(m_intent);
    }

    /// Get SQL string (requires dialect for intent-based queries).
    /// For manual SQL, returns the pre-rendered string.
    const std::string &sql() const {
        if (!m_manual_sql) {
            throw std::runtime_error("QueryAdapter: call render(dialect) for intent-based queries, "
                                     "or use sql() only for manual SQL queries");
        }
        return m_rendered_sql;
    }

    /// Get params as py::object list (for Python-side introspection).
    std::vector<py::object> params_py() const {
        const auto &source = m_manual_sql ? m_rendered_params : collect_intent_params();
        std::vector<py::object> result;
        result.reserve(source.size());
        for (const auto &v : source) {
            result.push_back(DataExtractor::cell_to_py(v));
        }
        return result;
    }

private:
    core::QueryIntent m_intent;
    bool m_manual_sql{false};
    std::string m_rendered_sql;
    std::vector<core::CellValue> m_rendered_params;

    void activate_builder() {
        if (!m_manual_sql) return;
        m_manual_sql = false;
        m_rendered_sql.clear();
        m_rendered_params.clear();
        m_intent = core::QueryIntent{};
    }

    std::vector<core::CellValue> collect_intent_params() const {
        std::vector<core::CellValue> params;
        params.reserve(m_intent.where_clauses.size());
        for (const auto &wc : m_intent.where_clauses) {
            params.push_back(wc.param);
        }
        return params;
    }
};

} // namespace pygim::adapter
