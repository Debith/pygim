// persistence/strategy/mssql/table_schema.h
// Target-table catalog introspection (sys.columns) and pre-save schema
// validation with per-column diagnostics (design doc 2.4).
//
// Distinct from schema_describe.h, which describes a RESULT SET after
// SQLPrepare; this reads the catalog, so identity/computed/default flags
// are available before any write is attempted.

#pragma once

#include "dialect.h"
#include "odbc_error.h"
#include "sql_helpers.h"
#include "stmt_handle.h"

#include <arrow/table.h>
#include <arrow/type.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::strategy::mssql {

/// One target-table column, straight from sys.columns / sys.types.
struct TableColumn {
    std::string name;
    std::string type_name;   //!< sys.types name (e.g. "nvarchar", "bigint")
    int         max_length{0};  //!< bytes; -1 = (MAX)
    int         precision{0};
    int         scale{0};
    bool        nullable{true};
    bool        is_identity{false};
    bool        is_computed{false};
    bool        has_default{false};
};

namespace detail {

[[nodiscard]] inline std::string fetch_cell(SQLHSTMT stmt, SQLUSMALLINT col) {
    char buf[520];  // sysname = 128 chars; up to 4 UTF-8 bytes each
    SQLLEN ind = 0;
    SQLRETURN ret = SQLGetData(stmt, col, SQL_C_CHAR, buf, sizeof(buf), &ind);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "table_schema: SQLGetData");
    if (ind == SQL_NULL_DATA) return {};
    return std::string(buf, static_cast<std::size_t>(
                                std::min<SQLLEN>(ind, static_cast<SQLLEN>(sizeof(buf) - 1))));
}

[[nodiscard]] inline int parse_catalog_int(const std::string& text) {
    try {
        return text.empty() ? 0 : std::stoi(text);
    } catch (const std::exception&) {
        throw std::runtime_error("table_schema: unexpected catalog value '" + text + "'");
    }
}

}  // namespace detail

/// Fetch the target table's column catalog. Empty result = table not found.
/// `qualified` must already have passed sql::qualify_table (safe to embed).
/// sys.columns/sys.types are per-database views, so 3-part names query the
/// TARGET database's catalog, and temp tables query tempdb.
[[nodiscard]] inline std::vector<TableColumn>
fetch_table_schema(SQLHDBC dbc, const std::string& qualified) {
    std::string catalog_prefix;   // "[db]." for cross-database targets
    std::string object_ref = qualified;
    if (!qualified.empty() && qualified.front() == '#') {
        catalog_prefix = "tempdb.";
        object_ref = "tempdb.." + qualified;
    } else {
        const auto first_dot = qualified.find('.');
        const auto second_dot =
            first_dot == std::string::npos ? std::string::npos
                                           : qualified.find('.', first_dot + 1);
        if (second_dot != std::string::npos) {
            // database.schema.table — db part already identifier-validated
            catalog_prefix = "[" + qualified.substr(0, first_dot) + "].";
        }
    }
    const std::string query =
        "SELECT c.name, t.name, c.max_length, c.precision, c.scale, "
        "c.is_nullable, c.is_identity, c.is_computed, "
        "CASE WHEN c.default_object_id <> 0 THEN 1 ELSE 0 END "
        "FROM " + catalog_prefix + "sys.columns c "
        "JOIN " + catalog_prefix + "sys.types t ON c.user_type_id = t.user_type_id "
        "WHERE c.object_id = OBJECT_ID(N'" + object_ref + "') ORDER BY c.column_id";

    StmtHandle stmt(dbc);
    SQLRETURN ret = SQLExecDirect(
        stmt, const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(query.c_str())), SQL_NTS);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "table_schema: SQLExecDirect");

    std::vector<TableColumn> columns;
    while (true) {
        ret = SQLFetch(stmt);
        if (ret == SQL_NO_DATA) break;
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "table_schema: SQLFetch");
        TableColumn col;
        col.name        = detail::fetch_cell(stmt, 1);
        col.type_name   = detail::fetch_cell(stmt, 2);
        col.max_length  = detail::parse_catalog_int(detail::fetch_cell(stmt, 3));
        col.precision   = detail::parse_catalog_int(detail::fetch_cell(stmt, 4));
        col.scale       = detail::parse_catalog_int(detail::fetch_cell(stmt, 5));
        col.nullable    = detail::fetch_cell(stmt, 6) == "1";
        col.is_identity = detail::fetch_cell(stmt, 7) == "1";
        col.is_computed = detail::fetch_cell(stmt, 8) == "1";
        col.has_default = detail::fetch_cell(stmt, 9) == "1";
        columns.push_back(std::move(col));
    }
    return columns;
}

namespace detail {

enum class TypeFamily { Numeric, String, Temporal, Binary, Guid, Other };

[[nodiscard]] inline TypeFamily sql_type_family(std::string_view t) {
    if (t == "tinyint" || t == "smallint" || t == "int" || t == "bigint" ||
        t == "bit" || t == "decimal" || t == "numeric" || t == "money" ||
        t == "smallmoney" || t == "float" || t == "real") return TypeFamily::Numeric;
    if (t == "char" || t == "varchar" || t == "nchar" || t == "nvarchar" ||
        t == "text" || t == "ntext" || t == "sysname") return TypeFamily::String;
    if (t == "date" || t == "datetime" || t == "datetime2" || t == "smalldatetime" ||
        t == "datetimeoffset" || t == "time") return TypeFamily::Temporal;
    if (t == "binary" || t == "varbinary" || t == "image" ||
        t == "timestamp" || t == "rowversion") return TypeFamily::Binary;
    if (t == "uniqueidentifier") return TypeFamily::Guid;
    return TypeFamily::Other;
}

[[nodiscard]] inline TypeFamily arrow_type_family(const arrow::DataType& t) {
    switch (t.id()) {
        case arrow::Type::BOOL:
        case arrow::Type::INT8:  case arrow::Type::INT16:
        case arrow::Type::INT32: case arrow::Type::INT64:
        case arrow::Type::UINT8: case arrow::Type::UINT16:
        case arrow::Type::UINT32: case arrow::Type::UINT64:
        case arrow::Type::HALF_FLOAT: case arrow::Type::FLOAT:
        case arrow::Type::DOUBLE:
        case arrow::Type::DECIMAL128: case arrow::Type::DECIMAL256:
            return TypeFamily::Numeric;
        case arrow::Type::STRING: case arrow::Type::LARGE_STRING:
        case arrow::Type::STRING_VIEW:  // polars >= 1.x emits view types
            return TypeFamily::String;
        case arrow::Type::DATE32: case arrow::Type::DATE64:
        case arrow::Type::TIMESTAMP: case arrow::Type::TIME32:
        case arrow::Type::TIME64:
            return TypeFamily::Temporal;
        case arrow::Type::BINARY: case arrow::Type::LARGE_BINARY:
        case arrow::Type::FIXED_SIZE_BINARY: case arrow::Type::BINARY_VIEW:
            return TypeFamily::Binary;
        default:
            return TypeFamily::Other;
    }
}

[[nodiscard]] inline std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

}  // namespace detail

/// Case-insensitive frame column lookup; -1 when absent.
[[nodiscard]] inline int find_frame_index(const arrow::Table& frame, const std::string& name) {
    const auto folded = detail::ascii_lower(name);
    for (int i = 0; i < frame.num_columns(); ++i) {
        if (detail::ascii_lower(frame.schema()->field(i)->name()) == folded) return i;
    }
    return -1;
}

/// Validate the frame's schema against the target table before any write.
/// Fails fast with a single error naming every offending column, covering:
/// unknown columns, NULLs headed into NOT NULL columns, required target
/// columns absent from the frame, and cross-family type mismatches.
/// (Strings ↔ GUIDs are allowed: uniqueidentifier loads/saves as text.)
inline void validate_frame_against_table(const arrow::Table& frame,
                                         const std::vector<TableColumn>& table_columns,
                                         const std::string& qualified) {
    if (table_columns.empty()) {
        throw odbc::OdbcError("Schema validation: table does not exist: " + qualified,
                              "", 0, odbc::ErrorKind::Data);
    }

    // Duplicate frame column names can never map onto a SQL table, and
    // Arrow's by-name lookups return null for them — reject up front.
    {
        std::vector<std::string> seen;
        std::string dups;
        for (const auto& field : frame.schema()->fields()) {
            auto folded = detail::ascii_lower(field->name());
            if (std::ranges::find(seen, folded) != seen.end()) {
                dups += (dups.empty() ? "" : ", ") + field->name();
            }
            seen.push_back(std::move(folded));
        }
        if (!dups.empty()) {
            throw odbc::OdbcError(
                "Schema validation failed for " + qualified +
                ": duplicate frame column names: [" + dups + "]",
                "", 0, odbc::ErrorKind::Data);
        }
    }

    // Case-insensitive matching: SQL Server catalogs default to CI collations.
    auto find_col = [&](const std::string& name) -> const TableColumn* {
        const auto folded = detail::ascii_lower(name);
        for (const auto& c : table_columns) {
            if (detail::ascii_lower(c.name) == folded) return &c;
        }
        return nullptr;
    };

    std::string unknown, null_into_notnull, type_mismatch;
    for (int i = 0; i < frame.num_columns(); ++i) {
        const auto& field = frame.schema()->field(i);
        const TableColumn* col = find_col(field->name());
        if (col == nullptr) {
            unknown += (unknown.empty() ? "" : ", ") + field->name();
            continue;
        }
        const auto frame_nulls = frame.column(i)->null_count();
        if (frame_nulls > 0 && !col->nullable) {
            null_into_notnull += (null_into_notnull.empty() ? "" : ", ") +
                field->name() + " (" + std::to_string(frame_nulls) + " NULLs -> NOT NULL)";
        }
        const auto ff = detail::arrow_type_family(*field->type());
        const auto tf = detail::sql_type_family(col->type_name);
        const bool compatible =
            ff == tf ||
            ff == detail::TypeFamily::Other || tf == detail::TypeFamily::Other ||
            // uniqueidentifier accepts a GUID as text OR as its 16 raw bytes
            // (fixed_size_binary[16], which is what create_df/BCP produce).
            (tf == detail::TypeFamily::Guid &&
             (ff == detail::TypeFamily::String || ff == detail::TypeFamily::Binary));
        if (!compatible) {
            type_mismatch += (type_mismatch.empty() ? "" : ", ") + field->name() +
                " (frame " + field->type()->ToString() + " vs table " + col->type_name + ")";
        }
    }

    std::string missing_required;
    for (const auto& col : table_columns) {
        if (col.nullable || col.is_identity || col.is_computed || col.has_default) continue;
        if (find_frame_index(frame, col.name) < 0) {
            missing_required += (missing_required.empty() ? "" : ", ") + col.name;
        }
    }

    if (unknown.empty() && null_into_notnull.empty() &&
        type_mismatch.empty() && missing_required.empty()) {
        return;
    }
    std::string message = "Schema validation failed for " + qualified + ":";
    if (!unknown.empty())           message += " columns not in table: [" + unknown + "];";
    if (!missing_required.empty())  message += " NOT NULL table columns missing from frame (no default): [" + missing_required + "];";
    if (!null_into_notnull.empty()) message += " NULLs headed into NOT NULL columns: [" + null_into_notnull + "];";
    if (!type_mismatch.empty())     message += " type mismatches: [" + type_mismatch + "];";
    throw odbc::OdbcError(message, "", 0, odbc::ErrorKind::Data);
}

/// Plain-append BCP binds frame column i to table ordinal i+1 — names are
/// never consulted by that write path. A frame with the right names in the
/// wrong ORDER would silently write values into different columns, so
/// append mode additionally requires exact positional correspondence.
inline void validate_frame_ordinals(const arrow::Table& frame,
                                    const std::vector<TableColumn>& table_columns,
                                    const std::string& qualified) {
    std::string problems;
    const auto n = static_cast<std::size_t>(frame.num_columns());
    if (n != table_columns.size()) {
        problems = "frame has " + std::to_string(n) + " columns, table has " +
                   std::to_string(table_columns.size()) +
                   " (append binds by position and requires all table columns, in order)";
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            const auto& fname = frame.schema()->field(static_cast<int>(i))->name();
            if (detail::ascii_lower(fname) != detail::ascii_lower(table_columns[i].name)) {
                problems += (problems.empty() ? "" : ", ");
                problems += "position " + std::to_string(i + 1) + ": frame '" + fname +
                            "' vs table '" + table_columns[i].name + "'";
            }
        }
    }
    if (!problems.empty()) {
        throw odbc::OdbcError(
            "Schema validation failed for " + qualified +
            " (append is positional): " + problems,
            "", 0, odbc::ErrorKind::Data);
    }
}

} // namespace pygim::strategy::mssql
