// persistence/core/query.h
// Fluent builder for SQL queries (intent storage).
//
// Two modes: (1) raw SQL via string constructor, or (2) structured intent
// via select()/from_table()/where()/limit(). Dialect renders the intent
// into backend-specific SQL. load() accepts both Query and raw strings.

#pragma once

#include "../../utils/logging.h"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pygim::core {

/// One bound query parameter. std::monostate represents SQL NULL. Order
/// matters for the pybind11 variant caster: bool must precede int64_t so
/// Python bools do not coerce to integers.
using QueryParam = std::variant<std::monostate, bool, std::int64_t, double, std::string>;


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
    std::vector<QueryParam>  m_params;

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

    /// Raw SQL with bound parameters ('?' markers, ODBC order).
    Query(std::string_view raw_sql, std::vector<QueryParam> params)
        : m_raw_sql(raw_sql)
        , m_params(std::move(params))
    {
        PYGIM_LOG_FMT("[Query] from raw SQL with %zu params\n", m_params.size());
    }

    /// Add a predicate. Repeated calls AND-combine (each clause is
    /// parenthesized when combined, so precedence stays intuitive).
    Query& where(std::string_view clause) {
        append_where(clause);
        return *this;
    }

    /// Add a predicate with bound parameters ('?' markers, ODBC order).
    /// Example: where("status = ?", {std::string("Approved")}).
    Query& where(std::string_view clause, std::vector<QueryParam> params) {
        append_where(clause);
        for (auto& p : params) m_params.push_back(std::move(p));
        return *this;
    }

    /// Membership predicate: renders "[col] IN (?, ?, ...)" with one bound
    /// parameter per value. An empty value list matches nothing ("1 = 0"),
    /// the correct semantics of an empty IN-set.
    Query& where_in(std::string_view column, std::vector<QueryParam> values) {
        if (values.empty()) {
            append_where("1 = 0");
            return *this;
        }
        // SQL Server caps a single statement at 2100 parameters; each value
        // here is one bound parameter, so refuse oversized lists with a clear
        // message rather than letting the driver fail cryptically at execute.
        if (values.size() > 2000) {
            throw std::runtime_error(
                "where_in: " + std::to_string(values.size()) +
                " values exceeds SQL Server's ~2100 parameter limit; "
                "filter in batches or stage the values in a temp table");
        }
        std::string clause = quote_column(column) + " IN (";
        for (std::size_t i = 0; i < values.size(); ++i) {
            clause += (i == 0) ? "?" : ", ?";
        }
        clause += ")";
        append_where(clause);
        for (auto& v : values) m_params.push_back(std::move(v));
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
    [[nodiscard]] std::vector<QueryParam> const& params() const { return m_params; }

    /// A query is "plain" when it selects a whole table with no filtering:
    /// only plain queries are eligible for range-partitioned parallel loads
    /// (the partition path builds its own SQL and would drop any predicate).
    [[nodiscard]] bool is_plain_table_scan() const {
        return !is_raw() && m_columns.empty() && m_where.empty() &&
               !m_limit.has_value() && m_params.empty();
    }

private:
    void append_where(std::string_view clause) {
        if (m_where.empty()) {
            m_where = clause;
        } else {
            m_where = "(" + m_where + ") AND (" + std::string(clause) + ")";
        }
        PYGIM_LOG_FMT("[Query] where => \"%s\"\n", m_where.c_str());
    }

    /// Bracket-quote a (possibly dotted) column identifier; rejects anything
    /// that is not a simple identifier chain to keep injection impossible.
    [[nodiscard]] static std::string quote_column(std::string_view column) {
        std::string out;
        std::size_t start = 0;
        while (true) {
            auto dot = column.find('.', start);
            auto part = column.substr(start, dot == std::string_view::npos
                                                 ? std::string_view::npos
                                                 : dot - start);
            if (part.empty() || (part[0] >= '0' && part[0] <= '9')) {
                throw std::runtime_error("where_in: invalid column identifier: " +
                                         std::string(column));
            }
            for (char c : part) {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '_';
                if (!ok) {
                    throw std::runtime_error("where_in: invalid column identifier: " +
                                             std::string(column));
                }
            }
            if (!out.empty()) out += ".";
            out += "[" + std::string(part) + "]";
            if (dot == std::string_view::npos) break;
            start = dot + 1;
        }
        return out;
    }
};

} // namespace pygim::core
