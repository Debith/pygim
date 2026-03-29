// repository/strategy/mssql/dialect.h
// MssqlDialect — renders Query intent into T-SQL.
// SELECT TOP N [col1], [col2] FROM [table] WHERE ...

#pragma once

#include "../../core/query.h"
#include <string>
#include <string_view>

namespace pygim::strategy::mssql {

using pygim::core::Query;

/// MssqlDialect — Renders Query intent into T-SQL.
///
/// Produces: SELECT TOP N [col1], [col2] FROM [table] WHERE ...
/// Identifier quoting follows SQL Server rules: [brackets] with ] doubled.
struct MssqlDialect {
    [[nodiscard]] std::string render(Query const& q) const {
        // Pre-compute estimated SQL length to avoid repeated reallocations
        std::size_t estimate = 20;  // "SELECT TOP NNN " overhead
        for (auto const& col : q.columns())
            estimate += col.size() + 4;  // [col],
        estimate += q.table().size() + 10;  // " FROM [table]"
        estimate += q.where_clause().size() + 8;  // " WHERE ..."

        std::string sql;
        sql.reserve(estimate);
        sql += "SELECT ";
        if (q.limit_value()) {
            sql += "TOP " + std::to_string(*q.limit_value()) + " ";
        }
        if (q.columns().empty()) {
            sql += "*";
        } else {
            for (std::size_t i = 0; i < q.columns().size(); ++i) {
                if (i > 0) sql += ", ";
                sql += quote_identifier(q.columns()[i]);
            }
        }
        sql += " FROM " + quote_identifier(q.table());
        if (!q.where_clause().empty()) {
            sql += " WHERE " + std::string(q.where_clause());
        }
        return sql;
    }

    /// Quote a SQL Server identifier with square brackets.
    /// Escapes embedded ']' by doubling: "my]col" → "[my]]col]".
    [[nodiscard]] std::string quote_identifier(std::string_view id) const {
        std::string result;
        result.reserve(id.size() + 2);
        result += '[';
        for (char c : id) {
            if (c == ']') result += ']';
            result += c;
        }
        result += ']';
        return result;
    }
};

} // namespace pygim::strategy::mssql
