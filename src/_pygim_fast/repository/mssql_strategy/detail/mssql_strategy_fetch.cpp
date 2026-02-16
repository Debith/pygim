#include "../mssql_strategy.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include "helpers.h"
#include "value_objects.h"
#include "../../../utils/logging.h"

namespace py = pybind11;

namespace pygim {

namespace {
#if PYGIM_HAVE_ODBC
struct StatementBinder {
    SQLHSTMT stmt;
    std::vector<long long> int_storage;
    std::vector<std::string> str_storage;
    std::vector<SQLLEN> indicators;

    StatementBinder(SQLHSTMT handle, size_t reserve)
        : stmt(handle), indicators(reserve, 0) {
        PYGIM_SCOPE_LOG_TAG("repo.fetch");
        int_storage.reserve(reserve);
        str_storage.reserve(reserve);
    }

    void bind_all(const std::vector<py::object> &params) {
        PYGIM_SCOPE_LOG_TAG("repo.fetch");
        indicators.assign(params.size(), 0);
        for (size_t i = 0; i < params.size(); ++i) {
            bind_one(static_cast<SQLUSMALLINT>(i + 1), params[i], indicators[i]);
        }
    }

    void bind_one(SQLUSMALLINT position, const py::object &value, SQLLEN &indicator) {
        PYGIM_SCOPE_LOG_TAG("repo.fetch");
        SQLRETURN ret = SQL_SUCCESS;
        if (value.is_none()) {
            indicator = SQL_NULL_DATA;
            static const char *dummy = "";
            ret = SQLBindParameter(stmt, position, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 0, 0,
                                   (SQLPOINTER)dummy, 0, &indicator);
        } else if (pybind11::isinstance<pybind11::int_>(value)) {
            int_storage.push_back(value.cast<long long>());
            indicator = 0;
            ret = SQLBindParameter(stmt, position, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                                   &int_storage.back(), 0, &indicator);
        } else {
            str_storage.emplace_back(pybind11::str(value));
            indicator = (SQLLEN)str_storage.back().size();
            ret = SQLBindParameter(stmt, position, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, indicator, 0,
                                   (SQLPOINTER)str_storage.back().c_str(), 0, &indicator);
        }
        if (!SQL_SUCCEEDED(ret)) {
            MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter");
        }
    }
};

struct NonQueryResult {
    bool success{false};
    SQLLEN row_count{0};
};

NonQueryResult execute_non_query(SQLHDBC dbc, const detail::QueryEnvelope &env) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    NonQueryResult result;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) {
        throw std::runtime_error("ODBC: alloc stmt failed");
    }
    SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)env.query.sql().c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare");
    }
    auto params = env.query.params();
    StatementBinder binder(stmt, params.size());
    binder.bind_all(params);
    ret = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, env.label.c_str());
    }
    SQLRowCount(stmt, &result.row_count);
    result.success = true;
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return result;
}

detail::QueryEnvelope build_update_envelope(const std::string &table,
                                            const std::vector<std::string> &columns,
                                            const std::vector<py::object> &values,
                                            const py::object &pk) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    std::string sql = "UPDATE " + table + " SET ";
    std::vector<py::object> params;
    params.reserve(values.size() + 1);
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i)
            sql.push_back(',');
        sql.append(columns[i]).append("=?");
        params.push_back(values[i]);
    }
    sql.append(" WHERE id=?");
    params.push_back(pk);
    return detail::QueryEnvelope{Query(sql, params), "update"};
}

detail::QueryEnvelope build_insert_envelope(const std::string &table,
                                            const std::vector<std::string> &columns,
                                            const std::vector<py::object> &values,
                                            const py::object &pk) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    std::string sql = "INSERT INTO " + table + " (id";
    std::vector<py::object> params;
    params.reserve(values.size() + 1);
    params.push_back(pk);
    for (const auto &col : columns) {
        sql.append(",").append(col);
    }
    sql.append(") VALUES (?" );
    for (size_t i = 0; i < columns.size(); ++i) {
        sql.append(",?");
        params.push_back(values[i]);
    }
    sql.append(")");
    return detail::QueryEnvelope{Query(sql, params), "insert"};
}

detail::QueryEnvelope envelope_from_pyquery(const py::object &query_obj) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    std::string sql = query_obj.attr("sql").cast<std::string>();
    py::list params_list = query_obj.attr("params")();
    std::vector<py::object> params;
    params.reserve(static_cast<size_t>(py::len(params_list)));
    for (py::handle h : params_list) {
        params.emplace_back(py::reinterpret_borrow<py::object>(h));
    }
    std::string label = "query:" + std::string(py::str(query_obj.attr("__class__").attr("__name__")));
    return detail::QueryEnvelope{Query(sql, params), label};
}
#endif
} // namespace

py::object MssqlStrategyNative::fetch(const py::object &key) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
#if PYGIM_HAVE_ODBC
    ensure_connected();
    if (py::hasattr(key, "sql") && py::hasattr(key, "params")) {
        try {
            return execute_query_object(key);
        } catch (...) {
            return py::none();
        }
    }
    std::string table = detail::extract_table(key);
    py::object pk = detail::extract_pk(key);
    if (!detail::is_valid_identifier(table))
        throw std::runtime_error("Invalid table identifier");
    try {
        return fetch_impl(table, pk);
    } catch (...) {
        return py::none();
    }
#else
    throw std::runtime_error("MssqlStrategyNative built without ODBC headers; feature unavailable");
#endif
}

void MssqlStrategyNative::save(const py::object &key, const py::object &value) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
#if PYGIM_HAVE_ODBC
    ensure_connected();
    std::string table = detail::extract_table(key);
    py::object pk = detail::extract_pk(key);
    if (!detail::is_valid_identifier(table))
        throw std::runtime_error("Invalid table identifier");
    upsert_impl(table, pk, value);
#else
    throw std::runtime_error("MssqlStrategyNative built without ODBC headers; feature unavailable");
#endif
}

#if PYGIM_HAVE_ODBC
py::object MssqlStrategyNative::fetch_impl(const std::string &table, const py::object &pk) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt) != SQL_SUCCESS)
        throw std::runtime_error("ODBC: alloc stmt failed");
    Query builder;
    builder.select(std::vector<std::string>{"*"}).from_table(table).where("id=?", pk);
    Query built_query = builder.build();
    const std::string &sql = built_query.sql();
    const auto params = built_query.params();
    SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)sql.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare");
    }
    StatementBinder binder(stmt, params.size());
    binder.bind_all(params);
    ret = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return py::none();
    }
    ret = SQLFetch(stmt);
    if (ret == SQL_NO_DATA) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return py::none();
    }
    SQLSMALLINT colCount = 0;
    SQLNumResultCols(stmt, &colCount);
    py::dict row;
    for (SQLSMALLINT i = 1; i <= colCount; ++i) {
        char colName[128];
        SQLSMALLINT nameLen = 0, dataType = 0, scale = 0, nullable = 0;
        SQLULEN colSize = 0;
        SQLDescribeCol(stmt, i, (SQLCHAR *)colName, sizeof(colName), &nameLen, &dataType, &colSize, &scale, &nullable);
        std::vector<char> buf(colSize + 16);
        SQLLEN outLen = 0;
        SQLRETURN dret = SQLGetData(stmt, i, SQL_C_CHAR, buf.data(), buf.size(), &outLen);
        if (SQL_SUCCEEDED(dret)) {
            if (outLen == SQL_NULL_DATA)
                row[colName] = py::none();
            else
                row[colName] = py::str(buf.data());
        } else
            row[colName] = py::none();
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return std::move(row);
}

void MssqlStrategyNative::upsert_impl(const std::string &table, const py::object &pk, const py::object &value_mapping) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    if (!py::isinstance<py::dict>(value_mapping))
        throw std::runtime_error("MssqlStrategyNative.save expects a dict-like value");
    py::dict d = value_mapping.cast<py::dict>();
    std::vector<std::string> columns;
    std::vector<py::object> values;
    for (auto &item : d) {
        std::string col = py::str(item.first);
        if (!detail::is_valid_identifier(col))
            continue;
        columns.push_back(col);
        values.push_back(py::reinterpret_borrow<py::object>(item.second));
    }
    if (columns.empty())
        return;
    auto update_env = build_update_envelope(table, columns, values, pk);
    NonQueryResult upd = execute_non_query(m_dbc, update_env);
    if (upd.success && upd.row_count > 0)
        return;
    auto insert_env = build_insert_envelope(table, columns, values, pk);
    execute_non_query(m_dbc, insert_env);
}

py::object MssqlStrategyNative::execute_query_object(const py::object &query_obj) {
    PYGIM_SCOPE_LOG_TAG("repo.fetch");
    auto env = envelope_from_pyquery(query_obj);
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt) != SQL_SUCCESS)
        throw std::runtime_error("ODBC: alloc stmt failed");
    std::string sql = env.query.sql();
    size_t limit_pos = sql.rfind(" LIMIT ");
    if (limit_pos != std::string::npos) {
        std::string before = sql.substr(0, limit_pos);
        std::string after = sql.substr(limit_pos + 7);
        int n = 0;
        try {
            n = std::stoi(after);
        } catch (...) {
            n = 0;
        }
        if (n > 0 && before.rfind("SELECT", 0) == 0)
            sql = "SELECT TOP " + std::to_string(n) + before.substr(6);
    }
    SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)sql.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare");
    }
    auto params = env.query.params();
    StatementBinder binder(stmt, params.size());
    binder.bind_all(params);
    ret = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return py::none();
    }
    SQLSMALLINT colCount = 0;
    SQLNumResultCols(stmt, &colCount);
    py::list rows;
    while (true) {
        ret = SQLFetch(stmt);
        if (ret == SQL_NO_DATA)
            break;
        if (!SQL_SUCCEEDED(ret))
            break;
        py::dict row;
        for (SQLSMALLINT c = 1; c <= colCount; ++c) {
            char colName[128];
            SQLSMALLINT nameLen = 0, dataType = 0, scale = 0, nullable = 0;
            SQLULEN colSize = 0;
            SQLDescribeCol(stmt, c, (SQLCHAR *)colName, sizeof(colName), &nameLen, &dataType, &colSize, &scale, &nullable);
            std::vector<char> buf(colSize + 16);
            SQLLEN outLen = 0;
            SQLRETURN dret = SQLGetData(stmt, c, SQL_C_CHAR, buf.data(), buf.size(), &outLen);
            if (SQL_SUCCEEDED(dret)) {
                if (outLen == SQL_NULL_DATA)
                    row[colName] = py::none();
                else
                    row[colName] = py::str(buf.data());
            } else
                row[colName] = py::none();
        }
        rows.append(std::move(row));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return rows;
}
#endif

} // namespace pygim
