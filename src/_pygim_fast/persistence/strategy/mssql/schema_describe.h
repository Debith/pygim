// repository/strategy/mssql/schema_describe.h
// Shared column description utility for load pipelines.
// Extracts column metadata via SQLDescribeCol after SQLPrepare (no execute needed).

#pragma once

#include "odbc_error.h"
#include "sql_type_map.h"

#include <arrow/type.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pygim::strategy::mssql {

/// Column metadata extracted from an ODBC statement.
struct SchemaInfo {
    std::shared_ptr<arrow::Schema> schema;
    std::vector<std::pair<std::string, TypeMapping>> col_info;
    std::vector<bool> nullable_flags;
};

/// Describe columns on a prepared (or executed) statement.
/// MSSQL ODBC supports SQLDescribeCol after SQLPrepare without SQLExecute.
[[nodiscard]]
inline SchemaInfo describe_columns(SQLHSTMT stmt) {
    SchemaInfo info;

    SQLSMALLINT num_cols = 0;
    SQLRETURN ret = SQLNumResultCols(stmt, &num_cols);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLNumResultCols");

    info.col_info.reserve(static_cast<std::size_t>(num_cols));
    info.nullable_flags.reserve(static_cast<std::size_t>(num_cols));

    for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(num_cols); ++i) {
        SQLCHAR col_name[256];
        SQLSMALLINT name_len   = 0;
        SQLSMALLINT sql_type   = 0;
        SQLULEN     col_size   = 0;
        SQLSMALLINT dec_digits = 0;
        SQLSMALLINT nullable   = 0;

        ret = SQLDescribeCol(stmt, i, col_name,
                             static_cast<SQLSMALLINT>(sizeof(col_name)),
                             &name_len, &sql_type, &col_size,
                             &dec_digits, &nullable);
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLDescribeCol");

        info.nullable_flags.push_back(nullable != SQL_NO_NULLS);
        auto mapping = resolve_type(sql_type, col_size, dec_digits);
        info.col_info.emplace_back(
            std::string(reinterpret_cast<const char*>(col_name),
                        static_cast<std::size_t>(name_len)),
            std::move(mapping));
    }

    info.schema = build_schema(info.col_info);
    return info;
}

} // namespace pygim::strategy::mssql
