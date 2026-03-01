// QueryDialect: abstract base for rendering QueryIntent into database-specific SQL.
// This header is pybind-free.
//
// Each database strategy owns a dialect. The strategy (or repository core)
// calls dialect.render(intent) to produce a RenderedQuery, then executes
// the SQL + params against the connection.
#pragma once

#include <string>
#include <vector>

#include "query_intent.h"
#include "value_types.h"

namespace pygim::core {

/// The output of rendering a QueryIntent — ready to execute.
struct RenderedQuery {
    std::string sql;
    std::vector<CellValue> params;
};

/// Abstract base: one subclass per SQL dialect (MSSQL, Postgres, …).
class QueryDialect {
public:
    virtual ~QueryDialect() = default;

    /// Render a full SELECT statement from intent.
    virtual RenderedQuery render(const QueryIntent &intent) const = 0;

    /// Quote a single identifier (table/column name) for this dialect.
    virtual std::string quote_identifier(const std::string &id) const = 0;
};

} // namespace pygim::core
