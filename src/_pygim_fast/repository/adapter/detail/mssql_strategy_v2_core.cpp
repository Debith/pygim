// MssqlStrategy v2 implementation: connection management, fetch, save, persist.
// Pybind-free — all methods operate on core C++ types only.

#include "../../mssql_strategy/mssql_strategy_v2.h"
#include "../../mssql_strategy/detail/bcp/bcp_entry.h"

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "../../mssql_strategy/detail/odbc_error.h"
#include "../../mssql_strategy/detail/cell_statement_binder.h"
#include "../../../utils/logging.h"

namespace pygim::mssql {

// ---- Construction / Destruction ---------------------------------------------

MssqlStrategy::MssqlStrategy(std::string connection_string)
    : m_conn_str(std::move(connection_string)) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.connection");
    init_handles();
}

MssqlStrategy::~MssqlStrategy() {
    PYGIM_SCOPE_LOG_TAG("repo.v2.connection");
    cleanup_handles();
}

// ---- core::Strategy interface -----------------------------------------------

core::StrategyCapabilities MssqlStrategy::capabilities() const {
    return {
        .can_fetch = true,
        .can_save = true,
        .can_persist = true,
    };
}

const core::QueryDialect *MssqlStrategy::dialect() const {
    return &m_dialect;
}

std::string MssqlStrategy::repr() const {
    return "MssqlStrategy(connected)";
}

// ---- Public method implementations ------------------------------------------

std::optional<core::ResultSet> MssqlStrategy::fetch(const core::RenderedQuery &query) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.fetch");
    ensure_connected();
    return fetch_impl(query.sql, query.params);
}

void MssqlStrategy::save(const core::TablePkKey &key, const core::RowMap &data) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.save");
    ensure_connected();
    upsert_impl(key.table, key.pk, data);
}

void MssqlStrategy::persist(const core::TableSpec &table_spec,
                             core::DataView view,
                             const core::PersistOptions &opts) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.persist");
    ensure_connected();

    std::visit([&](auto &&data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, core::ArrowView>) {
            bcp::bulk_insert_arrow_bcp(m_dbc, table_spec.name,
                                       std::move(data.reader),
                                       data.input_mode,
                                       opts.batch_size,
                                       table_spec.table_hint,
                                       m_last_bcp_metrics);
        } else if constexpr (std::is_same_v<T, core::TypedBatchView>) {
            if (opts.mode == core::PersistMode::Upsert) {
                const std::string key_col = opts.key_column.value_or("id");
                bulk_upsert_typed(table_spec.name, data.batch, key_col,
                                  opts.batch_size, table_spec.table_hint);
            } else {
                bulk_insert_typed(table_spec.name, data.batch,
                                  opts.batch_size, table_spec.table_hint);
            }
        }
    }, std::move(view));
}

// ---- ODBC Handle Management -------------------------------------------------

void MssqlStrategy::init_handles() {
    PYGIM_SCOPE_LOG_TAG("repo.v2.connection");
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env) != SQL_SUCCESS) {
        throw std::runtime_error("ODBC: Failed to allocate env handle");
    }
    SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_dbc) != SQL_SUCCESS) {
        throw std::runtime_error("ODBC: Failed to allocate dbc handle");
    }
}

void MssqlStrategy::cleanup_handles() {
    PYGIM_SCOPE_LOG_TAG("repo.v2.connection");
    if (m_dbc != SQL_NULL_HDBC) {
        SQLDisconnect(m_dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, m_dbc);
        m_dbc = SQL_NULL_HDBC;
    }
    if (m_env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, m_env);
        m_env = SQL_NULL_HENV;
    }
    m_connected = false;
    m_bcp_attr_enabled = false;
}

void MssqlStrategy::ensure_connected() {
    PYGIM_SCOPE_LOG_TAG("repo.v2.connection");
    if (m_dbc == SQL_NULL_HDBC) {
        throw std::runtime_error("ODBC: dbc handle null");
    }
    if (m_connected) return;

    if (!m_bcp_attr_enabled) {
        SQLRETURN attr_ret = SQLSetConnectAttr(
            m_dbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            raise_if_error(attr_ret, SQL_HANDLE_DBC, m_dbc, "SQLSetConnectAttr(BCP_ON)");
        }
        m_bcp_attr_enabled = true;
    }

    SQLSMALLINT outstrlen = 0;
    SQLRETURN ret = SQLDriverConnect(
        m_dbc, NULL, (SQLCHAR *)m_conn_str.c_str(), SQL_NTS,
        NULL, 0, &outstrlen, SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(ret)) {
        raise_if_error(ret, SQL_HANDLE_DBC, m_dbc, "SQLDriverConnect");
    }
    m_connected = true;
}

void MssqlStrategy::raise_if_error(SQLRETURN ret, SQLSMALLINT type,
                                   SQLHANDLE handle, const char *what) {
    odbc::raise_if_error(ret, type, handle, what);
}

// ---- Fetch implementation ---------------------------------------------------

std::optional<core::ResultSet> MssqlStrategy::fetch_impl(
    const std::string &sql,
    const std::vector<core::CellValue> &params) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.fetch");
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt) != SQL_SUCCESS) {
        throw std::runtime_error("ODBC: alloc stmt failed");
    }

    SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)sql.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare");
    }

    // Bind parameters using CellStatementBinder.
    detail::CellStatementBinder binder;
    binder.bind_all(stmt, params);

    ret = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::nullopt;
    }

    // Discover columns.
    SQLSMALLINT col_count = 0;
    SQLNumResultCols(stmt, &col_count);

    core::ResultSet result;
    result.columns.reserve(static_cast<size_t>(col_count));

    // Collect column metadata.
    std::vector<SQLSMALLINT> col_types(static_cast<size_t>(col_count));
    for (SQLSMALLINT i = 1; i <= col_count; ++i) {
        char col_name[128];
        SQLSMALLINT name_len = 0, data_type = 0, scale = 0, nullable = 0;
        SQLULEN col_size = 0;
        SQLDescribeCol(stmt, i, (SQLCHAR *)col_name, sizeof(col_name),
                       &name_len, &data_type, &col_size, &scale, &nullable);
        result.columns.emplace_back(col_name, name_len);
        col_types[static_cast<size_t>(i - 1)] = data_type;
    }

    // Fetch rows.
    while (true) {
        ret = SQLFetch(stmt);
        if (ret == SQL_NO_DATA) break;
        if (!SQL_SUCCEEDED(ret)) break;

        std::vector<core::CellValue> row;
        row.reserve(static_cast<size_t>(col_count));

        for (SQLSMALLINT c = 1; c <= col_count; ++c) {
            SQLLEN out_len = 0;
            SQLSMALLINT dtype = col_types[static_cast<size_t>(c - 1)];

            // Try typed retrieval for numeric types.
            if (dtype == SQL_BIGINT || dtype == SQL_INTEGER ||
                dtype == SQL_SMALLINT || dtype == SQL_TINYINT) {
                int64_t val = 0;
                SQLRETURN dret = SQLGetData(stmt, c, SQL_C_SBIGINT, &val, 0, &out_len);
                if (SQL_SUCCEEDED(dret) && out_len != SQL_NULL_DATA) {
                    row.push_back(val);
                    continue;
                }
            } else if (dtype == SQL_FLOAT || dtype == SQL_DOUBLE || dtype == SQL_REAL) {
                double val = 0.0;
                SQLRETURN dret = SQLGetData(stmt, c, SQL_C_DOUBLE, &val, 0, &out_len);
                if (SQL_SUCCEEDED(dret) && out_len != SQL_NULL_DATA) {
                    row.push_back(val);
                    continue;
                }
            } else if (dtype == SQL_BIT) {
                unsigned char val = 0;
                SQLRETURN dret = SQLGetData(stmt, c, SQL_C_BIT, &val, 0, &out_len);
                if (SQL_SUCCEEDED(dret) && out_len != SQL_NULL_DATA) {
                    row.push_back(val != 0);
                    continue;
                }
            }

            // Fallback: retrieve as string.
            std::vector<char> buf(1024);
            SQLRETURN dret = SQLGetData(stmt, c, SQL_C_CHAR, buf.data(),
                                        static_cast<SQLLEN>(buf.size()), &out_len);
            if (SQL_SUCCEEDED(dret)) {
                if (out_len == SQL_NULL_DATA) {
                    row.push_back(core::Null{});
                } else {
                    row.emplace_back(std::string(buf.data()));
                }
            } else {
                row.push_back(core::Null{});
            }
        }
        result.rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return result;
}

// ---- Save (upsert) implementation ------------------------------------------

void MssqlStrategy::upsert_impl(const std::string &table,
                                const core::CellValue &pk,
                                const core::RowMap &data) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.save");
    if (data.empty()) return;

    // Build column/value lists from RowMap.
    std::vector<std::string> columns;
    std::vector<core::CellValue> values;
    columns.reserve(data.size());
    values.reserve(data.size());
    for (const auto &[col, val] : data) {
        columns.push_back(col);
        values.push_back(val);
    }

    // Try UPDATE first.
    {
        std::string sql = "UPDATE " + table + " SET ";
        std::vector<core::CellValue> params;
        params.reserve(values.size() + 1);
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i) sql.push_back(',');
            sql.append(columns[i]).append("=?");
            params.push_back(values[i]);
        }
        sql.append(" WHERE id=?");
        params.push_back(pk);

        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt) != SQL_SUCCESS) {
            throw std::runtime_error("ODBC: alloc stmt failed");
        }
        SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)sql.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare(UPDATE)");
        }
        detail::CellStatementBinder binder;
        binder.bind_all(stmt, params);
        ret = SQLExecute(stmt);
        if (SQL_SUCCEEDED(ret)) {
            SQLLEN row_count = 0;
            SQLRowCount(stmt, &row_count);
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            if (row_count > 0) return; // Update succeeded.
        } else {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    // Fallback: INSERT.
    {
        std::string sql = "INSERT INTO " + table + " (id";
        std::vector<core::CellValue> params;
        params.reserve(values.size() + 1);
        params.push_back(pk);
        for (const auto &col : columns) {
            sql.append(",").append(col);
        }
        sql.append(") VALUES (?");
        for (size_t i = 0; i < columns.size(); ++i) {
            sql.append(",?");
            params.push_back(values[i]);
        }
        sql.append(")");

        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt) != SQL_SUCCESS) {
            throw std::runtime_error("ODBC: alloc stmt failed");
        }
        SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)sql.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare(INSERT)");
        }
        detail::CellStatementBinder binder;
        binder.bind_all(stmt, params);
        ret = SQLExecute(stmt);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLExecute(INSERT)");
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }
}

} // namespace pygim::mssql
