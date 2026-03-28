// repository/core/query.h
// Core C++ package — Query builder placeholder.
//
// Fluent builder for SQL queries.  Backend validates dialect compatibility.
// load() accepts both Query objects and raw strings (D9).

#pragma once

#include "../../utils/logging.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::core {

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

    std::string build() const {
        if (!m_raw_sql.empty()) {
            PYGIM_LOG_FMT("[Query] build() → raw SQL\n");
            return m_raw_sql;
        }

        std::string sql = "SELECT ";
        if (m_columns.empty()) {
            sql += "*";
        } else {
            for (std::size_t i = 0; i < m_columns.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += m_columns[i];
            }
        }
        sql += " FROM " + m_table;
        if (!m_where.empty())
            sql += " WHERE " + m_where;
        if (m_limit)
            sql += " TOP " + std::to_string(*m_limit);

        PYGIM_LOG_FMT("[Query] build() → \"%s\"\n", sql.c_str());
        return sql;
    }

    bool is_raw() const { return !m_raw_sql.empty(); }
    std::string_view table() const { return m_table; }
};

} // namespace pygim::core
