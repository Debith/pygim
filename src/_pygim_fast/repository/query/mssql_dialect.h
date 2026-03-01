// MSSQL dialect: renders QueryIntent into T-SQL.
// This header is pybind-free.
//
// Follows MSSQL conventions:
// - SELECT TOP N instead of LIMIT
// - [bracket] identifier quoting
// - ? parameter placeholders
#pragma once

#include <string>
#include <vector>

#include "../core/query_dialect.h"
#include "../core/query_intent.h"
#include "../core/value_types.h"

namespace pygim::query {

class MssqlDialect final : public core::QueryDialect {
public:
    core::RenderedQuery render(const core::QueryIntent &intent) const override {
        core::RenderedQuery out;
        write_select(out.sql, intent.columns, intent.limit);
        write_from(out.sql, intent.table);
        write_where(out.sql, intent.where_clauses, out.params);
        write_order_by(out.sql, intent.order_by);
        return out;
    }

    std::string quote_identifier(const std::string &id) const override {
        if (id.empty()) return {};
        return "[" + id + "]";
    }

private:
    static void write_select(std::string &sql,
                             const std::vector<std::string> &columns,
                             const std::optional<int> &limit) {
        sql.append("SELECT ");
        if (limit.has_value() && *limit > 0) {
            sql.append("TOP ");
            sql.append(std::to_string(*limit));
            sql.push_back(' ');
        }
        if (columns.empty()) {
            sql.push_back('*');
        } else {
            for (size_t i = 0; i < columns.size(); ++i) {
                if (i) sql.push_back(',');
                sql.append(columns[i]);
            }
        }
    }

    static void write_from(std::string &sql, const std::string &table) {
        sql.append(" FROM ");
        sql.append(table);
    }

    static void write_where(std::string &sql,
                            const std::vector<core::WhereClause> &clauses,
                            std::vector<core::CellValue> &params) {
        if (clauses.empty()) return;
        sql.append(" WHERE ");
        for (size_t i = 0; i < clauses.size(); ++i) {
            if (i) sql.append(" AND ");
            sql.append(clauses[i].expression);
            params.push_back(clauses[i].param);
        }
    }

    static void write_order_by(std::string &sql,
                               const std::vector<core::OrderSpec> &order) {
        if (order.empty()) return;
        sql.append(" ORDER BY ");
        for (size_t i = 0; i < order.size(); ++i) {
            if (i) sql.push_back(',');
            sql.append(order[i].column);
            if (order[i].descending) sql.append(" DESC");
        }
    }
};

} // namespace pygim::query
