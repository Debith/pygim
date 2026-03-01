// SQL builders for typed batch operations — pybind-free.
// Generates multi-row INSERT and MERGE T-SQL using core value types.
#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace pygim::mssql::detail {

/// Builds multi-row INSERT INTO ... VALUES (...),(...) statements.
class TypedInsertBuilder {
public:
    TypedInsertBuilder(std::string table,
                       std::vector<std::string> columns,
                       std::string table_hint)
        : m_table(std::move(table)),
          m_columns(std::move(columns)),
          m_table_hint(std::move(table_hint)) {
        if (m_columns.empty()) {
            throw std::invalid_argument("TypedInsertBuilder requires at least one column");
        }
    }

    [[nodiscard]] std::string build(int rows_per_statement) const {
        if (rows_per_statement <= 0) {
            throw std::invalid_argument("rows_per_statement must be positive");
        }
        std::string sql;
        sql.reserve(static_cast<size_t>(rows_per_statement) * m_columns.size() * 4 + 128);
        sql.append("INSERT INTO ");
        sql.append(m_table);
        append_table_hint(sql);
        sql.append(" (");
        append_column_list(sql, m_columns);
        sql.append(") VALUES ");
        for (int row = 0; row < rows_per_statement; ++row) {
            if (row) sql.push_back(',');
            sql.push_back('(');
            append_placeholders(sql, m_columns.size());
            sql.push_back(')');
        }
        return sql;
    }

    static void append_placeholders(std::string &sql, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (i) sql.push_back(',');
            sql.push_back('?');
        }
    }

private:
    void append_column_list(std::string &sql, const std::vector<std::string> &cols) const {
        for (size_t i = 0; i < cols.size(); ++i) {
            if (i) sql.push_back(',');
            sql.append(cols[i]);
        }
    }

    void append_table_hint(std::string &sql) const {
        if (m_table_hint.empty()) return;
        sql.append(" WITH (");
        sql.append(m_table_hint);
        sql.push_back(')');
    }

    std::string m_table;
    std::vector<std::string> m_columns;
    std::string m_table_hint;
};

/// Builds multi-row MERGE INTO ... USING (VALUES ...) statements.
class TypedMergeBuilder {
public:
    TypedMergeBuilder(std::string table,
                      std::vector<std::string> columns,
                      std::string key_column,
                      std::string table_hint)
        : m_table(std::move(table)),
          m_columns(std::move(columns)),
          m_key_column(std::move(key_column)),
          m_table_hint(std::move(table_hint)) {
        if (m_columns.empty()) {
            throw std::invalid_argument("TypedMergeBuilder requires at least one column");
        }
        for (const auto &col : m_columns) {
            if (col != m_key_column) {
                m_non_key_columns.push_back(col);
            }
        }
    }

    [[nodiscard]] std::string build(int rows_per_statement) const {
        if (rows_per_statement <= 0) {
            throw std::invalid_argument("rows_per_statement must be positive");
        }
        std::string sql;
        sql.reserve(static_cast<size_t>(rows_per_statement) * m_columns.size() * 6 + 256);
        sql.append("MERGE INTO ");
        sql.append(m_table);
        append_table_hint(sql);
        sql.append(" AS target USING (VALUES ");
        for (int row = 0; row < rows_per_statement; ++row) {
            if (row) sql.push_back(',');
            sql.push_back('(');
            TypedInsertBuilder::append_placeholders(sql, m_columns.size());
            sql.push_back(')');
        }
        sql.append(") AS source (");
        append_column_list(sql, m_columns);
        sql.append(") ON target.");
        sql.append(m_key_column);
        sql.append(" = source.");
        sql.append(m_key_column);
        if (!m_non_key_columns.empty()) {
            sql.append(" WHEN MATCHED THEN UPDATE SET ");
            for (size_t i = 0; i < m_non_key_columns.size(); ++i) {
                if (i) sql.push_back(',');
                sql.append("target.");
                sql.append(m_non_key_columns[i]);
                sql.append("=source.");
                sql.append(m_non_key_columns[i]);
            }
        }
        sql.append(" WHEN NOT MATCHED THEN INSERT (");
        append_column_list(sql, m_columns);
        sql.append(") VALUES (");
        for (size_t i = 0; i < m_columns.size(); ++i) {
            if (i) sql.push_back(',');
            sql.append("source.");
            sql.append(m_columns[i]);
        }
        sql.append(");");
        return sql;
    }

private:
    void append_column_list(std::string &sql, const std::vector<std::string> &cols) const {
        for (size_t i = 0; i < cols.size(); ++i) {
            if (i) sql.push_back(',');
            sql.append(cols[i]);
        }
    }

    void append_table_hint(std::string &sql) const {
        if (m_table_hint.empty()) return;
        sql.append(" WITH (");
        sql.append(m_table_hint);
        sql.push_back(')');
    }

    std::string m_table;
    std::vector<std::string> m_columns;
    std::string m_key_column;
    std::string m_table_hint;
    std::vector<std::string> m_non_key_columns;
};

/// Compute optimal rows per statement given column count and parameter limit.
inline int compute_rows_per_stmt(size_t ncols, int batch_size, int param_limit = 2090) {
    if (ncols == 0) return 0;
    return std::max(1, std::min(batch_size, param_limit / static_cast<int>(ncols)));
}

} // namespace pygim::mssql::detail
