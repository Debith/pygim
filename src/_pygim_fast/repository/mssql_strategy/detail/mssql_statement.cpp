#include "mssql_statement.h"

#include <stdexcept>

#include "../mssql_strategy.h"
#include "../../../utils/logging.h"

namespace pygim::detail {

StatementTemplate::StatementTemplate(SQLHDBC dbc, const BatchSpec &spec)
    : dbc_(dbc),
      table_spec_(spec.to_table_spec()),
      builder_(table_spec_),
      full_rows_(spec.rows_per_stmt()) {
}

StatementTemplate::~StatementTemplate() {
    release(full_stmt_);
    release(tail_stmt_);
}

SQLHSTMT StatementTemplate::get_or_prepare(int rows_this) {
    PYGIM_SCOPE_LOG_TAG("repo.statement");
    if (rows_this <= 0) {
        throw std::runtime_error("rows_this must be positive");
    }
    if (rows_this == full_rows_) {
        if (full_stmt_ == SQL_NULL_HSTMT) {
            full_stmt_ = prepare_statement(rows_this);
        }
        return full_stmt_;
    }
    if (rows_this != tail_rows_) {
        release(tail_stmt_);
        tail_stmt_ = prepare_statement(rows_this);
        tail_rows_ = rows_this;
    }
    return tail_stmt_;
}

SQLHSTMT StatementTemplate::prepare_statement(int rows_this) {
    PYGIM_SCOPE_LOG_TAG_MSG("repo.statement", "prepare_statement");
    if (SQLHSTMT stmt = SQL_NULL_HSTMT; true) {
        if (SQLAllocHandle(SQL_HANDLE_STMT, dbc_, &stmt) != SQL_SUCCESS) {
            throw std::runtime_error("ODBC: alloc stmt failed");
        }
        const std::string sql = builder_.build(rows_this);
        SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR *)sql.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            try {
                MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare");
            } catch (...) {
                SQLFreeHandle(SQL_HANDLE_STMT, stmt);
                throw;
            }
        }
        return stmt;
    }
}

void StatementTemplate::release(SQLHSTMT &stmt) {
    PYGIM_SCOPE_LOG_TAG_MSG("repo.statement", "release");
    if (stmt != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        stmt = SQL_NULL_HSTMT;
    }
}

} // namespace pygim::detail
