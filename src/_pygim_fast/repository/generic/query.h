#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace pygim {
namespace py = pybind11;

class Query {
public:
    Query(std::string sql, std::vector<py::object> params)
        : m_sql(std::move(sql)), m_params(std::move(params)) {}
    const std::string& sql() const noexcept { return m_sql; }
    std::vector<py::object> params() const { return m_params; }
    std::string repr() const { return "Query(sql='"+m_sql+"', params=" + std::to_string(m_params.size()) + ")"; }
private:
    std::string m_sql;
    std::vector<py::object> m_params;
};

class QueryBuilder {
public:
    QueryBuilder& select(std::vector<std::string> columns) { m_columns = std::move(columns); return *this; }
    QueryBuilder& from(std::string table) { m_table = std::move(table); return *this; }
    QueryBuilder& where(std::string clause, py::object param) {
        m_whereClauses.push_back(std::move(clause));
        m_params.push_back(std::move(param));
        return *this;
    }
    QueryBuilder& limit(int n) { m_limit = n; return *this; }
    Query build() const {
        std::stringstream ss; ss << "SELECT ";
        if (m_columns.empty()) ss << "*"; else {
            for (size_t i=0;i<m_columns.size();++i) { if (i) ss << ','; ss << m_columns[i]; }
        }
        ss << " FROM " << m_table;
        if (!m_whereClauses.empty()) {
            ss << " WHERE ";
            for (size_t i=0;i<m_whereClauses.size(); ++i) { if (i) ss << " AND "; ss << m_whereClauses[i]; }
        }
        if (m_limit) ss << " LIMIT " << *m_limit;
        return Query(ss.str(), m_params);
    }
private:
    std::vector<std::string> m_columns; std::string m_table; std::vector<std::string> m_whereClauses; std::vector<py::object> m_params; std::optional<int> m_limit;
};

} // namespace pygim
