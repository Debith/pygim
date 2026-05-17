// persistence/strategy/mssql/sql_type_map.h
// Maps ODBC SQL types to Arrow types for the load pipeline.

#pragma once

#include <arrow/type.h>
#include <arrow/type_fwd.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#ifdef BOOL
#  undef BOOL
#endif
#ifdef INT
#  undef INT
#endif

#include "../../odbc_compat.h"

namespace pygim::strategy::mssql {

/// Upper bound on per-column fetch buffer to avoid unbounded allocation
/// (e.g. VARCHAR(MAX) reports col_size == 0).
inline constexpr int64_t kMaxColumnBuffer = 65536;

/// Everything the fetch buffer and Arrow builder need for one column.
struct TypeMapping {
    std::shared_ptr<arrow::DataType> arrow_type;
    SQLSMALLINT                      c_type{};
    int64_t                          element_width{};
};

/// Resolve an ODBC SQL type (from SQLDescribeCol) to the corresponding
/// Arrow type, C bind type, and per-element buffer width.
[[nodiscard]] inline TypeMapping resolve_type(SQLSMALLINT sql_type,
                                              SQLULEN     col_size,
                                              SQLSMALLINT /*decimal_digits*/) {
    switch (sql_type) {

    // ── boolean ──────────────────────────────────────────────────
    case SQL_BIT:
        return {arrow::boolean(), SQL_C_BIT, 1};

    // ── integers ─────────────────────────────────────────────────
    case SQL_TINYINT:
        return {arrow::int8(), SQL_C_STINYINT, 1};
    case SQL_SMALLINT:
        return {arrow::int16(), SQL_C_SSHORT, 2};
    case SQL_INTEGER:
        return {arrow::int32(), SQL_C_SLONG, 4};
    case SQL_BIGINT:
        return {arrow::int64(), SQL_C_SBIGINT, 8};

    // ── floating point ───────────────────────────────────────────
    case SQL_REAL:
        return {arrow::float32(), SQL_C_FLOAT, 4};
    case SQL_FLOAT:
    case SQL_DOUBLE:
        return {arrow::float64(), SQL_C_DOUBLE, 8};

    // ── decimal / numeric — lossy via double (documenting precision loss) ─
    case SQL_DECIMAL:
    case SQL_NUMERIC:
        return {arrow::float64(), SQL_C_DOUBLE, 8};

    // ── narrow strings ───────────────────────────────────────────
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR: {
        int64_t w = (col_size == 0) ? kMaxColumnBuffer
                                    : std::min<int64_t>(static_cast<int64_t>(col_size) + 1,
                                                        kMaxColumnBuffer);
        return {arrow::utf8(), SQL_C_CHAR, w};
    }

    // ── wide strings — driver converts UTF-16 → UTF-8 via SQL_C_CHAR ───
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR: {
        int64_t w = (col_size == 0) ? kMaxColumnBuffer
                                    : std::min<int64_t>(static_cast<int64_t>(col_size) * 4 + 1,
                                                        kMaxColumnBuffer);
        return {arrow::utf8(), SQL_C_CHAR, w};
    }

    // ── binary ───────────────────────────────────────────────────
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY: {
        int64_t w = (col_size == 0) ? kMaxColumnBuffer
                                    : std::min<int64_t>(static_cast<int64_t>(col_size),
                                                        kMaxColumnBuffer);
        return {arrow::binary(), SQL_C_BINARY, w};
    }

    // ── temporal ─────────────────────────────────────────────────
    case SQL_TYPE_DATE:
        return {arrow::date32(), SQL_C_TYPE_DATE,
                static_cast<int64_t>(sizeof(SQL_DATE_STRUCT))};

    case SQL_TYPE_TIMESTAMP:
#ifdef SQL_DATETIME
    case SQL_DATETIME:
#endif
        return {arrow::timestamp(arrow::TimeUnit::MICRO), SQL_C_TYPE_TIMESTAMP,
                static_cast<int64_t>(sizeof(SQL_TIMESTAMP_STRUCT))};

    case kSQL_SS_TIME2:  // SQL Server extended TIME type
        return {arrow::time64(arrow::TimeUnit::MICRO), SQL_C_BINARY,
                static_cast<int64_t>(sizeof(SQL_SS_TIME2_STRUCT))};

    // ── GUID ─────────────────────────────────────────────────────
    case SQL_GUID:
        return {arrow::fixed_size_binary(16), SQL_C_GUID, 16};

    default:
        throw std::runtime_error(
            "sql_type_map: unsupported SQL type " + std::to_string(sql_type));
    }
}

/// Build an Arrow schema from described column info.
[[nodiscard]] inline std::shared_ptr<arrow::Schema>
build_schema(const std::vector<std::pair<std::string, TypeMapping>>& col_info) {
    arrow::FieldVector fields;
    fields.reserve(col_info.size());
    for (const auto& [name, mapping] : col_info) {
        fields.push_back(arrow::field(name, mapping.arrow_type));
    }
    return arrow::schema(std::move(fields));
}

} // namespace pygim::strategy::mssql
