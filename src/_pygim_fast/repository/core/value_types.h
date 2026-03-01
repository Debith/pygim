// Core value types for the repository subsystem.
// This header is pybind-free — it must NOT include any pybind11 headers.
// All Python data extraction happens in the adapter layer; strategies
// and repository core operate exclusively on these C++ types.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace pygim::core {

// ---- Cell-level value -------------------------------------------------------

/// Sentinel representing SQL NULL.
struct Null {
    bool operator==(const Null &) const noexcept { return true; }
    bool operator!=(const Null &) const noexcept { return false; }
};

/// Universal cell value covering ODBC-compatible scalar types.
/// Ordered: Null < bool < int64 < double < string (for variant index).
using CellValue = std::variant<Null, bool, int64_t, double, std::string>;

// ---- Row-level types --------------------------------------------------------

/// A single row as column_name → value.
using RowMap = std::unordered_map<std::string, CellValue>;

// ---- Result set (returned by fetch) ----------------------------------------

/// Tabular result returned from a strategy fetch operation.
struct ResultSet {
    std::vector<std::string> columns;
    std::vector<std::vector<CellValue>> rows; // row-major: rows[row_idx][col_idx]

    [[nodiscard]] bool empty() const noexcept { return rows.empty(); }
    [[nodiscard]] size_t row_count() const noexcept { return rows.size(); }
    [[nodiscard]] size_t column_count() const noexcept { return columns.size(); }
};

// ---- Typed columnar batch (for bulk operations) ----------------------------

/// Column-major typed storage for bulk insert / upsert.
/// Each column stores a single primitive type plus a null mask.
/// This avoids per-cell variant overhead in hot bulk paths.
struct TypedColumnBatch {
    enum class Kind : uint8_t { I64, F64, BOOL, STR };

    struct Column {
        Kind kind{Kind::STR};
        std::vector<int64_t> i64_data;
        std::vector<double> f64_data;
        std::vector<uint8_t> bool_data;
        std::vector<std::string> str_data;
        std::vector<bool> null_mask; // true = null; empty = no nulls

        [[nodiscard]] bool has_nulls() const noexcept { return !null_mask.empty(); }
    };

    std::vector<std::string> column_names;
    std::vector<Column> columns;
    size_t row_count{0};
};

// ---- Key types for fetch / save --------------------------------------------

/// Identifies a row by table + primary key.
struct TablePkKey {
    std::string table;
    CellValue pk;
};

// ---- Table specification (shared across bulk helpers) ----------------------

struct TableSpec {
    std::string name;
    std::vector<std::string> columns;
    std::optional<std::string> key_column;
    std::string table_hint;

    [[nodiscard]] size_t column_count() const noexcept { return columns.size(); }
    [[nodiscard]] bool has_key() const noexcept { return key_column.has_value(); }
};

} // namespace pygim::core
