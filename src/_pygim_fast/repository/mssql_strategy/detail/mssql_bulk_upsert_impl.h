#pragma once

#include <algorithm>
#include <stdexcept>

#include "../mssql_strategy.h"
#include "mssql_batch_source.h"
#include "mssql_batch_spec.h"
#include "mssql_param_binder.h"
#include "mssql_statement.h"
#include "mssql_transaction.h"

namespace pygim::detail {

template <BatchRowSource Src>
void bulk_upsert_impl(SQLHDBC dbc, const BatchSpec &spec, const Src &source) {
    if (source.column_count() != spec.column_count()) {
        throw std::runtime_error("Column count mismatch between spec and source");
    }
    const size_t total_rows = source.row_count();
    if (total_rows == 0) {
        return;
    }
    const int rows_per_stmt = spec.rows_per_stmt();
    if (rows_per_stmt <= 0) {
        return;
    }

    TransactionGuard txn(dbc);
    StatementTemplate statements(dbc, spec);
    ParameterBinder binder;

    size_t offset = 0;
    while (offset < total_rows) {
        const size_t remaining = total_rows - offset;
        const int rows_this = static_cast<int>(std::min<size_t>(rows_per_stmt, remaining));
        SQLHSTMT stmt = statements.get_or_prepare(rows_this);
        binder.bind_all(stmt, source, offset, offset + static_cast<size_t>(rows_this));
        SQLRETURN ret = SQLExecute(stmt);
        if (!SQL_SUCCEEDED(ret)) {
            MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLExecute");
        }
        SQLFreeStmt(stmt, SQL_CLOSE);
        offset += static_cast<size_t>(rows_this);
    }
    txn.commit();
}

} // namespace pygim::detail
