// MssqlStrategy v2 bulk operations: INSERT and MERGE using TypedColumnBatch.
// Pybind-free — operates entirely on core C++ typed batch data.

#include "../../mssql_strategy/mssql_strategy_v2.h"

#include <algorithm>
#include <stdexcept>

#include "../../mssql_strategy/detail/cell_statement_binder.h"
#include "../../mssql_strategy/detail/typed_sql_builders.h"
#include "../../../utils/logging.h"

namespace pygim::mssql {

#if PYGIM_HAVE_ODBC

// ---- Bulk INSERT using TypedColumnBatch -------------------------------------

void MssqlStrategy::bulk_insert_typed(const std::string &table,
                                      const core::TypedColumnBatch &batch,
                                      int batch_size,
                                      const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.bulk");
    const size_t ncols = batch.columns.size();
    const size_t total_rows = batch.row_count;
    if (ncols == 0 || total_rows == 0) return;

    if (static_cast<int>(ncols) > 2090) {
        throw std::runtime_error("Too many columns for one INSERT");
    }

    const int effective_batch = batch_size > 0 ? batch_size : 1000;
    const int rows_per_stmt = detail::compute_rows_per_stmt(ncols, effective_batch);

    detail::TypedInsertBuilder builder(table, batch.column_names, table_hint);
    const std::string sql_full = builder.build(rows_per_stmt);

    // Transaction guard.
    SQLUINTEGER old_autocommit = SQL_AUTOCOMMIT_ON;
    SQLINTEGER outlen = 0;
    SQLGetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, &old_autocommit, 0, &outlen);
    SQLSetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);

    SQLHSTMT stmt_full = SQL_NULL_HSTMT;
    SQLHSTMT stmt_tail = SQL_NULL_HSTMT;
    int tail_rows = 0;

    try {
        if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt_full) != SQL_SUCCESS) {
            throw std::runtime_error("ODBC: alloc stmt failed");
        }
        SQLRETURN ret = SQLPrepare(stmt_full, (SQLCHAR *)sql_full.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
            stmt_full = SQL_NULL_HSTMT;
            raise_if_error(ret, SQL_HANDLE_STMT, stmt_full, "SQLPrepare(INSERT)");
        }

        size_t offset = 0;
        detail::CellStatementBinder binder;

        while (offset < total_rows) {
            const size_t remaining = total_rows - offset;
            const int rows_this = static_cast<int>(
                std::min<size_t>(static_cast<size_t>(rows_per_stmt), remaining));

            SQLHSTMT stmt = stmt_full;
            if (rows_this != rows_per_stmt) {
                // Need a tail statement for the last partial batch.
                if (rows_this != tail_rows) {
                    if (stmt_tail != SQL_NULL_HSTMT) {
                        SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
                        stmt_tail = SQL_NULL_HSTMT;
                    }
                    const std::string sql_tail = builder.build(rows_this);
                    if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt_tail) != SQL_SUCCESS) {
                        throw std::runtime_error("ODBC: alloc stmt failed");
                    }
                    ret = SQLPrepare(stmt_tail, (SQLCHAR *)sql_tail.c_str(), SQL_NTS);
                    if (!SQL_SUCCEEDED(ret)) {
                        SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
                        stmt_tail = SQL_NULL_HSTMT;
                        raise_if_error(ret, SQL_HANDLE_STMT, stmt_tail, "SQLPrepare(INSERT tail)");
                    }
                    tail_rows = rows_this;
                }
                stmt = stmt_tail;
            }

            binder.bind_batch(stmt, batch, offset, offset + static_cast<size_t>(rows_this));

            ret = SQLExecute(stmt);
            if (!SQL_SUCCEEDED(ret)) {
                raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLExecute(INSERT batch)");
            }
            SQLFreeStmt(stmt, SQL_CLOSE);
            offset += static_cast<size_t>(rows_this);
        }

        SQLEndTran(SQL_HANDLE_DBC, m_dbc, SQL_COMMIT);
    } catch (...) {
        SQLEndTran(SQL_HANDLE_DBC, m_dbc, SQL_ROLLBACK);
        if (stmt_full != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
        if (stmt_tail != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
        SQLSetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)old_autocommit, 0);
        throw;
    }

    if (stmt_full != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
    if (stmt_tail != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
    SQLSetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)old_autocommit, 0);
}

// ---- Bulk UPSERT (MERGE) using TypedColumnBatch -----------------------------

void MssqlStrategy::bulk_upsert_typed(const std::string &table,
                                      const core::TypedColumnBatch &batch,
                                      const std::string &key_column,
                                      int batch_size,
                                      const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.v2.bulk");
    const size_t ncols = batch.columns.size();
    const size_t total_rows = batch.row_count;
    if (ncols == 0 || total_rows == 0) return;

    const int effective_batch = batch_size > 0 ? batch_size : 500;
    const int rows_per_stmt = detail::compute_rows_per_stmt(ncols, effective_batch);

    detail::TypedMergeBuilder builder(table, batch.column_names, key_column, table_hint);
    const std::string sql_full = builder.build(rows_per_stmt);

    // Transaction guard.
    SQLUINTEGER old_autocommit = SQL_AUTOCOMMIT_ON;
    SQLINTEGER outlen = 0;
    SQLGetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, &old_autocommit, 0, &outlen);
    SQLSetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);

    SQLHSTMT stmt_full = SQL_NULL_HSTMT;
    SQLHSTMT stmt_tail = SQL_NULL_HSTMT;
    int tail_rows = 0;

    try {
        if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt_full) != SQL_SUCCESS) {
            throw std::runtime_error("ODBC: alloc stmt failed");
        }
        SQLRETURN ret = SQLPrepare(stmt_full, (SQLCHAR *)sql_full.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
            stmt_full = SQL_NULL_HSTMT;
            raise_if_error(ret, SQL_HANDLE_STMT, stmt_full, "SQLPrepare(MERGE)");
        }

        size_t offset = 0;
        detail::CellStatementBinder binder;

        while (offset < total_rows) {
            const size_t remaining = total_rows - offset;
            const int rows_this = static_cast<int>(
                std::min<size_t>(static_cast<size_t>(rows_per_stmt), remaining));

            SQLHSTMT stmt = stmt_full;
            if (rows_this != rows_per_stmt) {
                if (rows_this != tail_rows) {
                    if (stmt_tail != SQL_NULL_HSTMT) {
                        SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
                        stmt_tail = SQL_NULL_HSTMT;
                    }
                    const std::string sql_tail = builder.build(rows_this);
                    if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt_tail) != SQL_SUCCESS) {
                        throw std::runtime_error("ODBC: alloc stmt failed");
                    }
                    ret = SQLPrepare(stmt_tail, (SQLCHAR *)sql_tail.c_str(), SQL_NTS);
                    if (!SQL_SUCCEEDED(ret)) {
                        SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
                        stmt_tail = SQL_NULL_HSTMT;
                        raise_if_error(ret, SQL_HANDLE_STMT, stmt_tail, "SQLPrepare(MERGE tail)");
                    }
                    tail_rows = rows_this;
                }
                stmt = stmt_tail;
            }

            binder.bind_batch(stmt, batch, offset, offset + static_cast<size_t>(rows_this));

            ret = SQLExecute(stmt);
            if (!SQL_SUCCEEDED(ret)) {
                raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLExecute(MERGE batch)");
            }
            SQLFreeStmt(stmt, SQL_CLOSE);
            offset += static_cast<size_t>(rows_this);
        }

        SQLEndTran(SQL_HANDLE_DBC, m_dbc, SQL_COMMIT);
    } catch (...) {
        SQLEndTran(SQL_HANDLE_DBC, m_dbc, SQL_ROLLBACK);
        if (stmt_full != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
        if (stmt_tail != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
        SQLSetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)old_autocommit, 0);
        throw;
    }

    if (stmt_full != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
    if (stmt_tail != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
    SQLSetConnectAttr(m_dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)old_autocommit, 0);
}

#endif // PYGIM_HAVE_ODBC

} // namespace pygim::mssql
