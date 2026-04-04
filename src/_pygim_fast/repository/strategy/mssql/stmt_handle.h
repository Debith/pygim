// repository/strategy/mssql/stmt_handle.h
// RAII wrapper for SQLHSTMT. Move-only, mirrors OdbcConnection pattern.

#pragma once

#include "odbc_error.h"   // includes <sql.h>, <sqlext.h>, undefs BOOL/INT

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

} // namespace pygim::strategy::mssql
