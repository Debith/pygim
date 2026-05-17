// persistence/strategy/mssql/pk_detect.h
// Auto-detect partition column for parallel load via ODBC metadata.
// Uses SQLPrimaryKeys() for PK discovery, SQLColumns() for type checking.

#pragma once

#include "odbc_error.h"
#include "stmt_handle.h"
#include "../../../utils/logging.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::strategy::mssql {

/// Parsed components of a qualified table name.
struct TableNameParts {
    std::string catalog;  // empty if not specified
    std::string schema;   // empty if not specified
    std::string table;    // always non-empty
};

/// Parse "catalog.schema.table", "schema.table", or "table" into parts.
/// Does NOT strip brackets — caller must pass unquoted names.
[[nodiscard]]
inline TableNameParts parse_table_name(std::string_view dotted_name) {
    auto dot1 = dotted_name.find('.');
    if (dot1 == std::string_view::npos) {
        return {"", "", std::string(dotted_name)};
    }
    auto dot2 = dotted_name.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos) {
        // schema.table
        return {"",
                std::string(dotted_name.substr(0, dot1)),
                std::string(dotted_name.substr(dot1 + 1))};
    }
    // catalog.schema.table
    return {std::string(dotted_name.substr(0, dot1)),
            std::string(dotted_name.substr(dot1 + 1, dot2 - dot1 - 1)),
            std::string(dotted_name.substr(dot2 + 1))};
}

/// Detect the best integer partition column for parallel load.
/// Uses SQLPrimaryKeys() to find PK columns, then checks their SQL data types.
/// Returns the first integer PK column name, or empty string if none found.
///
/// Integer SQL types checked: SQL_INTEGER, SQL_BIGINT, SQL_SMALLINT, SQL_TINYINT.
[[nodiscard]]
inline std::string detect_partition_column(SQLHDBC dbc, std::string_view table_name) {
    auto parts = parse_table_name(table_name);

    PYGIM_LOG_FMT("[pk_detect] detecting partition column for '%s' "
                  "(catalog='%s', schema='%s', table='%s')\n",
                  std::string(table_name).c_str(),
                  parts.catalog.c_str(), parts.schema.c_str(),
                  parts.table.c_str());

    // Step 1: Get PK column names via SQLPrimaryKeys
    StmtHandle pk_stmt(dbc);

    SQLCHAR* cat_ptr = parts.catalog.empty() ? nullptr
        : const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(parts.catalog.c_str()));
    SQLSMALLINT cat_len = parts.catalog.empty() ? 0 : SQL_NTS;

    SQLCHAR* sch_ptr = parts.schema.empty() ? nullptr
        : const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(parts.schema.c_str()));
    SQLSMALLINT sch_len = parts.schema.empty() ? 0 : SQL_NTS;

    SQLCHAR* tbl_ptr = const_cast<SQLCHAR*>(
        reinterpret_cast<const SQLCHAR*>(parts.table.c_str()));

    SQLRETURN ret = SQLPrimaryKeys(pk_stmt, cat_ptr, cat_len,
                                    sch_ptr, sch_len,
                                    tbl_ptr, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        PYGIM_LOG_FMT("[pk_detect] SQLPrimaryKeys failed — no auto-detect\n");
        return "";
    }

    // SQLPrimaryKeys result set columns:
    // 1=TABLE_CAT, 2=TABLE_SCHEM, 3=TABLE_NAME, 4=COLUMN_NAME, 5=KEY_SEQ, 6=PK_NAME
    struct PkColumn {
        std::string name;
        SQLSMALLINT key_seq;
    };
    std::vector<PkColumn> pk_columns;

    SQLCHAR col_name_buf[256];
    SQLSMALLINT key_seq = 0;
    SQLLEN ind_name = 0, ind_seq = 0;

    SQLBindCol(pk_stmt, 4, SQL_C_CHAR, col_name_buf, sizeof(col_name_buf), &ind_name);
    SQLBindCol(pk_stmt, 5, SQL_C_SSHORT, &key_seq, sizeof(key_seq), &ind_seq);

    while (SQLFetch(pk_stmt) == SQL_SUCCESS) {
        if (ind_name != SQL_NULL_DATA && ind_name > 0) {
            pk_columns.push_back({
                std::string(reinterpret_cast<const char*>(col_name_buf),
                            static_cast<std::size_t>(ind_name)),
                key_seq
            });
        }
    }

    if (pk_columns.empty()) {
        PYGIM_LOG_FMT("[pk_detect] no primary key found for '%s'\n",
                      std::string(table_name).c_str());
        return "";
    }

    // Sort by KEY_SEQ to ensure deterministic ordering
    std::ranges::sort(pk_columns, {}, &PkColumn::key_seq);

    PYGIM_LOG_FMT("[pk_detect] found %zu PK column(s)\n", pk_columns.size());

    // Step 2: Check PK column types via SQLColumns
    StmtHandle col_stmt(dbc);

    ret = SQLColumns(col_stmt, cat_ptr, cat_len,
                     sch_ptr, sch_len,
                     tbl_ptr, SQL_NTS,
                     nullptr, 0);  // all columns
    if (!SQL_SUCCEEDED(ret)) {
        PYGIM_LOG_FMT("[pk_detect] SQLColumns failed — cannot check types\n");
        return "";
    }

    // SQLColumns result set: 4=COLUMN_NAME, 5=DATA_TYPE
    struct ColType {
        std::string name;
        SQLSMALLINT data_type;
    };
    std::vector<ColType> col_types;

    SQLCHAR c_name_buf[256];
    SQLSMALLINT c_data_type = 0;
    SQLLEN c_ind_name = 0, c_ind_type = 0;

    SQLBindCol(col_stmt, 4, SQL_C_CHAR, c_name_buf, sizeof(c_name_buf), &c_ind_name);
    SQLBindCol(col_stmt, 5, SQL_C_SSHORT, &c_data_type, sizeof(c_data_type), &c_ind_type);

    while (SQLFetch(col_stmt) == SQL_SUCCESS) {
        if (c_ind_name != SQL_NULL_DATA && c_ind_name > 0) {
            col_types.push_back({
                std::string(reinterpret_cast<const char*>(c_name_buf),
                            static_cast<std::size_t>(c_ind_name)),
                c_data_type
            });
        }
    }

    // Step 3: Find first integer PK column
    auto is_integer_type = [](SQLSMALLINT sql_type) -> bool {
        return sql_type == SQL_INTEGER
            || sql_type == SQL_BIGINT
            || sql_type == SQL_SMALLINT
            || sql_type == SQL_TINYINT;
    };

    for (auto const& pk : pk_columns) {
        auto it = std::ranges::find_if(col_types,
            [&pk](auto const& ct) { return ct.name == pk.name; });
        if (it != col_types.end() && is_integer_type(it->data_type)) {
            PYGIM_LOG_FMT("[pk_detect] selected partition column: '%s' "
                          "(SQL type=%d, KEY_SEQ=%d)\n",
                          pk.name.c_str(), it->data_type, pk.key_seq);
            return pk.name;
        }
    }

    PYGIM_LOG_FMT("[pk_detect] no integer PK column found for '%s' — "
                  "falling back to single-threaded\n",
                  std::string(table_name).c_str());
    return "";
}

} // namespace pygim::strategy::mssql
