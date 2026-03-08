// MssqlStrategy bulk operations: INSERT and MERGE via a single
// bulk_persist_typed<Mode> template.
// Mode (PersistMode::Insert / PersistMode::Upsert) is a compile-time parameter;
// builder type, default batch size, and error labels are selected with
// if constexpr — the transaction + bind/execute loop is shared verbatim.
// Pybind-free — operates entirely on core C++ typed batch data.

#include "../../mssql_strategy/mssql_strategy.h"

#include <algorithm>
#include <stdexcept>

#include "../../mssql_strategy/detail/cell_statement_binder.h"
#include "../../mssql_strategy/detail/typed_sql_builders.h"
#include "../../../utils/logging.h"

namespace pygim::mssql {

template <typename Transpose>
template <core::PersistMode Mode>
void MssqlStrategy<Transpose>::bulk_persist_typed(const std::string &table,
                                                  const core::TypedColumnBatch &batch,
                                                  const std::string &key_column,
                                                  int batch_size,
                                                  const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.bulk");
    const size_t ncols = batch.columns.size();
    const size_t total_rows = batch.row_count;
    if (ncols == 0 || total_rows == 0) return;

    if constexpr (Mode == core::PersistMode::Insert) {
        if (static_cast<int>(ncols) > 2090) {
            throw std::runtime_error("Too many columns for one INSERT");
        }
    }

    // Compile-time default batch size: INSERT benefits from larger batches
    // (fewer round-trips, TABLOCK applies across the whole commit); MERGE is
    // more expensive per parameter so defaults to half.
    constexpr int default_batch = (Mode == core::PersistMode::Insert) ? 1000 : 500;
    const int effective_batch = batch_size > 0 ? batch_size : default_batch;
    const int rows_per_stmt = detail::compute_rows_per_stmt(ncols, effective_batch);

    // Builder construction — INSERT and MERGE have different constructor arities.
    auto builder = [&] {
        if constexpr (Mode == core::PersistMode::Insert)
            return detail::TypedInsertBuilder(table, batch.column_names, table_hint);
        else
            return detail::TypedMergeBuilder(table, batch.column_names, key_column, table_hint);
    }();

    const std::string sql_full = builder.build(rows_per_stmt);

    // Compile-time error-context labels — no string construction at runtime.
    constexpr const char *op_prepare      = (Mode == core::PersistMode::Insert)
                                            ? "SQLPrepare(INSERT)"
                                            : "SQLPrepare(MERGE)";
    constexpr const char *op_prepare_tail = (Mode == core::PersistMode::Insert)
                                            ? "SQLPrepare(INSERT tail)"
                                            : "SQLPrepare(MERGE tail)";
    constexpr const char *op_execute      = (Mode == core::PersistMode::Insert)
                                            ? "SQLExecute(INSERT batch)"
                                            : "SQLExecute(MERGE batch)";

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
            raise_if_error(ret, SQL_HANDLE_STMT, stmt_full, op_prepare);
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
                        raise_if_error(ret, SQL_HANDLE_STMT, stmt_tail, op_prepare_tail);
                    }
                    tail_rows = rows_this;
                }
                stmt = stmt_tail;
            }

            binder.bind_batch(stmt, batch, offset, offset + static_cast<size_t>(rows_this));

            ret = SQLExecute(stmt);
            if (!SQL_SUCCEEDED(ret)) {
                raise_if_error(ret, SQL_HANDLE_STMT, stmt, op_execute);
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

// ---- Explicit instantiations (2 Transpose × 2 Mode = 4) --------------------

template void MssqlStrategy<bcp::RowMajorTranspose>::bulk_persist_typed<core::PersistMode::Insert>(
    const std::string &, const core::TypedColumnBatch &, const std::string &, int, const std::string &);
template void MssqlStrategy<bcp::RowMajorTranspose>::bulk_persist_typed<core::PersistMode::Upsert>(
    const std::string &, const core::TypedColumnBatch &, const std::string &, int, const std::string &);
template void MssqlStrategy<bcp::ColumnMajorTranspose>::bulk_persist_typed<core::PersistMode::Insert>(
    const std::string &, const core::TypedColumnBatch &, const std::string &, int, const std::string &);
template void MssqlStrategy<bcp::ColumnMajorTranspose>::bulk_persist_typed<core::PersistMode::Upsert>(
    const std::string &, const core::TypedColumnBatch &, const std::string &, int, const std::string &);

} // namespace pygim::mssql
