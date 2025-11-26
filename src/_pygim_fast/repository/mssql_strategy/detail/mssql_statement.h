#pragma once

#include <sql.h>

#include "mssql_batch_spec.h"
#include "sql_builders.h"

namespace pygim::detail {

class StatementTemplate {
public:
    StatementTemplate(SQLHDBC dbc, const BatchSpec &spec);
    ~StatementTemplate();

    SQLHSTMT get_or_prepare(int rows_this);
    int full_rows() const noexcept { return full_rows_; }

private:
    SQLHSTMT prepare_statement(int rows_this);
    void release(SQLHSTMT &stmt);

    SQLHDBC dbc_;
    TableSpec table_spec_;
    MergeSqlBuilder builder_;
    int full_rows_{0};
    SQLHSTMT full_stmt_{SQL_NULL_HSTMT};
    SQLHSTMT tail_stmt_{SQL_NULL_HSTMT};
    int tail_rows_{0};
};

} // namespace pygim::detail
