// QueryIntent: a pure value object capturing query intent without rendering.
// This header is pybind-free.
//
// The user (or adapter) builds a QueryIntent describing what to fetch.
// A QueryDialect renders it into concrete SQL for a specific database.
// Strategies receive rendered queries — they never see QueryIntent directly.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "value_types.h"

namespace pygim::core {

/// A single WHERE predicate with its bound parameter.
struct WhereClause {
    std::string expression; // e.g. "id = ?" or "name LIKE ?"
    CellValue param;
};

/// Column ordering specification.
struct OrderSpec {
    std::string column;
    bool descending{false};
};

/// Database-agnostic query intent.
/// Captures *what* to query without knowing *how* (dialect renders the SQL).
struct QueryIntent {
    std::string table;
    std::vector<std::string> columns; // empty = SELECT *
    std::vector<WhereClause> where_clauses;
    std::optional<int> limit;
    std::vector<OrderSpec> order_by;

    // ---- Builder-style mutators (return *this for chaining) -----------------

    QueryIntent &select(std::vector<std::string> cols) {
        columns = std::move(cols);
        return *this;
    }

    QueryIntent &from_table(std::string tbl) {
        table = std::move(tbl);
        return *this;
    }

    QueryIntent &where(std::string expr, CellValue param) {
        where_clauses.push_back({std::move(expr), std::move(param)});
        return *this;
    }

    QueryIntent &set_limit(int n) {
        if (n <= 0)
            limit.reset();
        else
            limit = n;
        return *this;
    }

    QueryIntent &add_order(std::string col, bool desc = false) {
        order_by.push_back({std::move(col), desc});
        return *this;
    }
};

} // namespace pygim::core
