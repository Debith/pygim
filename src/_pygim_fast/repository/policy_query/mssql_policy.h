#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pygim::policy_query {

struct MssqlQueryPolicy {
    void write_select(std::string &sql, const std::vector<std::string> &columns) const;
    void write_from(std::string &sql, std::string_view table) const;
    void write_where(std::string &sql, const std::vector<std::string> &clauses) const;
    void write_limit(std::string &sql, int limit) const;
    void append_table_hint(std::string &sql, std::string_view hint) const;
    std::string quote_identifier(std::string_view identifier) const;
};

inline void MssqlQueryPolicy::write_select(std::string &sql, const std::vector<std::string> &columns) const {
    sql.append("SELECT ");
    if (columns.empty()) {
        sql.append("*");
        return;
    }
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i) {
            sql.push_back(',');
        }
        sql.append(columns[i]);
    }
}

inline void MssqlQueryPolicy::write_from(std::string &sql, std::string_view table) const {
    sql.append(" FROM ");
    sql.append(table);
}

inline void MssqlQueryPolicy::write_where(std::string &sql, const std::vector<std::string> &clauses) const {
    if (clauses.empty()) {
        return;
    }
    sql.append(" WHERE ");
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i) {
            sql.append(" AND ");
        }
        sql.append(clauses[i]);
    }
}

inline void MssqlQueryPolicy::write_limit(std::string &sql, int limit) const {
    if (limit <= 0) {
        return;
    }
    sql.append(" LIMIT ");
    sql.append(std::to_string(limit));
}

inline void MssqlQueryPolicy::append_table_hint(std::string &sql, std::string_view hint) const {
    if (hint.empty()) {
        return;
    }
    sql.append(" WITH (");
    sql.append(hint);
    sql.push_back(')');
}

inline std::string MssqlQueryPolicy::quote_identifier(std::string_view identifier) const {
    if (identifier.empty()) {
        return {};
    }
    // Conservatively avoid quoting unless necessary; hook for future.
    return std::string(identifier);
}

} // namespace pygim::policy_query
