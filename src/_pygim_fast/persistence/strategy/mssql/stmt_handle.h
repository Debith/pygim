// persistence/strategy/mssql/stmt_handle.h
// RAII wrapper for SQLHSTMT. Move-only, mirrors OdbcConnection pattern.

#pragma once

#include "odbc_error.h"   // includes <sql.h>, <sqlext.h>, undefs BOOL/INT

#include <algorithm>
#include <string>

namespace pygim::strategy::mssql {

/// StmtHandle — RAII owner of a single SQLHSTMT allocated from an SQLHDBC.
/// Move-only; destructor frees the handle if still valid.
class StmtHandle {
public:
    StmtHandle() = default;

    /// Allocate a statement handle from an active connection.
    explicit StmtHandle(SQLHDBC dbc) {
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &m_stmt);
        odbc::raise_if_error(ret, SQL_HANDLE_DBC, dbc,
                             "StmtHandle: SQLAllocHandle(SQL_HANDLE_STMT)");
    }

    ~StmtHandle() noexcept { free(); }

    // Move-only
    StmtHandle(StmtHandle&& other) noexcept
        : m_stmt(other.m_stmt) {
        other.m_stmt = SQL_NULL_HSTMT;
    }

    StmtHandle& operator=(StmtHandle&& other) noexcept {
        if (this != &other) {
            free();
            m_stmt = other.m_stmt;
            other.m_stmt = SQL_NULL_HSTMT;
        }
        return *this;
    }

    StmtHandle(const StmtHandle&) = delete;
    StmtHandle& operator=(const StmtHandle&) = delete;

    /// Raw handle accessor.
    [[nodiscard]] SQLHSTMT handle() const noexcept { return m_stmt; }

    /// Implicit conversion for direct use in ODBC APIs.
    operator SQLHSTMT() const noexcept { return m_stmt; }

private:
    void free() noexcept {
        if (m_stmt != SQL_NULL_HSTMT) {
            SQLFreeHandle(SQL_HANDLE_STMT, m_stmt);
            m_stmt = SQL_NULL_HSTMT;
        }
    }

    SQLHSTMT m_stmt{SQL_NULL_HSTMT};
};

/// Execute a statement with no result set (DDL, MERGE, DELETE, ...).
/// SQL_NO_DATA (e.g. a DELETE matching zero rows) counts as success.
/// Returns the affected row count, or -1 when not applicable (DDL).
inline long long exec_direct(SQLHDBC dbc, const std::string& sql) {
    StmtHandle stmt(dbc);
    SQLRETURN ret = SQLExecDirect(
        stmt, const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(sql.c_str())), SQL_NTS);
    if (ret != SQL_NO_DATA) {
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                             ("exec_direct: " + sql).c_str());
    }
    SQLLEN rows = -1;
    if (SQL_SUCCEEDED(SQLRowCount(stmt, &rows))) {
        return static_cast<long long>(rows);
    }
    return -1;
}

/// Execute a query returning at most one string cell; "" when the result
/// set is empty or the cell is NULL. Used for catalog probes.
[[nodiscard]] inline std::string query_scalar_string(SQLHDBC dbc, const std::string& sql) {
    StmtHandle stmt(dbc);
    SQLRETURN ret = SQLExecDirect(
        stmt, const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(sql.c_str())), SQL_NTS);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                         ("query_scalar_string: " + sql).c_str());
    ret = SQLFetch(stmt);
    if (ret == SQL_NO_DATA) {
        return {};
    }
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "query_scalar_string: SQLFetch");
    char buf[512];
    SQLLEN indicator = 0;
    ret = SQLGetData(stmt, 1, SQL_C_CHAR, buf, sizeof(buf), &indicator);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "query_scalar_string: SQLGetData");
    if (indicator == SQL_NULL_DATA) {
        return {};
    }
    const auto len = std::min<SQLLEN>(indicator, static_cast<SQLLEN>(sizeof(buf) - 1));
    return std::string(buf, static_cast<std::size_t>(len));
}

} // namespace pygim::strategy::mssql
