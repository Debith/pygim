#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mssql_batch_spec.h"
#include "value_objects.h"
#include "../../policy_query/mssql_policy.h"

namespace pygim::detail {

class InsertSqlBuilder {
public:
    InsertSqlBuilder(TableSpec spec, std::vector<std::string> columns)
        : m_spec(std::move(spec)), m_columns(std::move(columns)) {
        if (m_columns.empty()) {
            throw std::invalid_argument("InsertSqlBuilder requires at least one column");
        }
    }

    [[nodiscard]] std::string build(int rows_per_statement) const {
        if (rows_per_statement <= 0) {
            throw std::invalid_argument("InsertSqlBuilder rows_per_statement must be positive");
        }
        std::string sql;
        sql.reserve(static_cast<size_t>(rows_per_statement) * m_columns.size() * 4);
        sql.append("INSERT INTO ");
        sql.append(m_spec.name);
        m_policy.append_table_hint(sql, m_spec.table_hint);
        sql.append(" (");
        append_column_list(sql, m_columns);
        sql.append(") VALUES ");
        for (int row = 0; row < rows_per_statement; ++row) {
            if (row) {
                sql.append(",");
            }
            sql.push_back('(');
            append_placeholders(sql, m_columns.size());
            sql.push_back(')');
        }
        return sql;
    }

    static void append_placeholders(std::string &sql, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (i) {
                sql.push_back(',');
            }
            sql.push_back('?');
        }
    }

private:
    void append_column_list(std::string &sql, const std::vector<std::string> &columns) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i) {
                sql.push_back(',');
            }
            sql.append(m_policy.quote_identifier(columns[i]));
        }
    }

    TableSpec m_spec;
    std::vector<std::string> m_columns;
    policy_query::MssqlQueryPolicy m_policy;
};

class MergeSqlBuilder {
public:
    explicit MergeSqlBuilder(TableSpec spec)
        : m_spec(std::move(spec)), m_policy() {
        if (!m_spec.key_column) {
            throw std::invalid_argument("MergeSqlBuilder requires a key column");
        }
        if (m_spec.columns.empty()) {
            throw std::invalid_argument("MergeSqlBuilder requires at least one column");
        }
        for (const auto &col : m_spec.columns) {
            if (m_spec.key_column && col != *m_spec.key_column) {
                m_non_key_columns.push_back(col);
            }
        }
    }

    [[nodiscard]] std::string build(int rows_per_statement) const {
        if (rows_per_statement <= 0) {
            throw std::invalid_argument("MergeSqlBuilder rows_per_statement must be positive");
        }
        std::string sql;
        sql.reserve(static_cast<size_t>(rows_per_statement) * m_spec.columns.size() * 6);
        sql.append("MERGE INTO ");
        sql.append(m_spec.name);
        m_policy.append_table_hint(sql, m_spec.table_hint);
        sql.append(" AS target USING (VALUES ");
        for (int row = 0; row < rows_per_statement; ++row) {
            if (row) {
                sql.append(",");
            }
            sql.push_back('(');
            InsertSqlBuilder::append_placeholders(sql, m_spec.columns.size());
            sql.push_back(')');
        }
        sql.append(") AS source (");
        append_column_list(sql, m_spec.columns);
        sql.append(") ON target.");
        sql.append(m_policy.quote_identifier(*m_spec.key_column));
        sql.append(" = source.");
        sql.append(m_policy.quote_identifier(*m_spec.key_column));
        if (!m_non_key_columns.empty()) {
            sql.append(" WHEN MATCHED THEN UPDATE SET ");
            for (size_t i = 0; i < m_non_key_columns.size(); ++i) {
                if (i) {
                    sql.append(",");
                }
                const std::string &col = m_non_key_columns[i];
                const std::string quoted = m_policy.quote_identifier(col);
                sql.append("target.");
                sql.append(quoted);
                sql.append("=source.");
                sql.append(quoted);
            }
        }
        sql.append(" WHEN NOT MATCHED THEN INSERT (");
        append_column_list(sql, m_spec.columns);
        sql.append(") VALUES (");
        for (size_t i = 0; i < m_spec.columns.size(); ++i) {
            if (i) {
                sql.push_back(',');
            }
            const std::string quoted = m_policy.quote_identifier(m_spec.columns[i]);
            sql.append("source.");
            sql.append(quoted);
        }
        sql.append(");");
        return sql;
    }

private:
    void append_column_list(std::string &sql, const std::vector<std::string> &columns) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i) {
                sql.push_back(',');
            }
            sql.append(m_policy.quote_identifier(columns[i]));
        }
    }

    TableSpec m_spec;
    std::vector<std::string> m_non_key_columns;
    policy_query::MssqlQueryPolicy m_policy;
};

} // namespace pygim::detail
