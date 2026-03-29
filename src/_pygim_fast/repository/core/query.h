// repository/core/query.h
// Fluent builder for SQL queries (intent storage).
//
// Two modes: (1) raw SQL via string constructor, or (2) structured intent
// via select()/from_table()/where()/limit(). Dialect renders the intent
// into backend-specific SQL. load() accepts both Query and raw strings.

#pragma once

#include "../../utils/logging.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::core {

/// Fluent SQL query builder — stores intent, not SQL text.
///
/// Two usage modes:
///   1. Raw SQL: Query("SELECT * FROM t") → is_raw()=true; dialect passes through.
///   2. Structured: Query().select("id").from_table("t").limit(10) → dialect renders.
///
/// If both raw SQL and builder methods are used, raw SQL takes precedence
/// (is_raw() checks m_raw_sql non-empty).
class Query {
    std::string              m_table;
    std::vector<std::string> m_columns;
    std::string              m_where;
    std::optional<int>       m_limit;
    std::string              m_raw_sql;

public:
    Query() = default;

    // Raw SQL constructor
    explicit Query(std::string_view raw_sql)
        : m_raw_sql(raw_sql)
    {
        PYGIM_LOG_FMT("[Query] from raw SQL: \"%.*s\"\n",
                      static_cast<int>(raw_sql.size()), raw_sql.data());
    }

    Query& select(std::string_view col) {
        m_columns.emplace_back(col);
        PYGIM_LOG_FMT("[Query] select(\"%.*s\")\n",
                      static_cast<int>(col.size()), col.data());
        return *this;
    }

    Query& from_table(std::string_view table) {
        m_table = table;
        PYGIM_LOG_FMT("[Query] from_table(\"%.*s\")\n",
                      static_cast<int>(table.size()), table.data());
        return *this;
    }

    Query& where(std::string_view clause) {
        m_where = clause;
        PYGIM_LOG_FMT("[Query] where(\"%.*s\")\n",
                      static_cast<int>(clause.size()), clause.data());
        return *this;
    }

    Query& limit(int n) {
        m_limit = n;
        PYGIM_LOG_FMT("[Query] limit(%d)\n", n);
        return *this;
    }

    [[nodiscard]] bool is_raw() const { return !m_raw_sql.empty(); }
    [[nodiscard]] std::string_view table() const { return m_table; }
    [[nodiscard]] std::vector<std::string> const& columns() const { return m_columns; }
    [[nodiscard]] std::string_view where_clause() const { return m_where; }
    [[nodiscard]] std::optional<int> limit_value() const { return m_limit; }
    [[nodiscard]] std::string_view raw_sql() const { return m_raw_sql; }
};

} // namespace pygim::core
