// LoadStrategy: abstract interface for materializing SQL fetch results.
//
// Different implementations produce different output formats:
//   ArrowLoadStrategy  → Arrow RecordBatch (for Polars / zero-copy)
//   (future)           → Python dict, Pandas DataFrame, etc.
//
// The ODBC fetch loop in MssqlStrategy drives the strategy via typed
// cell-level callbacks, allowing each implementation to build its target
// representation directly without intermediate copies.
//
// This header is pybind-free and Arrow-free.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::core {

/// Neutral column type — mapped from ODBC SQL types by the fetch loop.
enum class ColumnType : uint8_t { Bool, Int64, Double, String };

/// Column metadata passed to LoadStrategy::begin().
struct ColumnInfo {
    std::string name;
    ColumnType type{ColumnType::String};
};

/// Abstract interface for materializing fetched rows into a target format.
///
/// Lifecycle:
///   1. begin(columns)         — receive column metadata, set up builders
///   2. For each row:
///      a. append_*(col, val)  — one call per cell
///      b. end_row()           — signal row boundary
///   3. finish()               — finalize output
///
/// Result retrieval is implementation-specific (e.g. ArrowLoadStrategy::result()).
class LoadStrategy {
public:
    virtual ~LoadStrategy() = default;

    /// Set up builders for the given columns.
    virtual void begin(const std::vector<ColumnInfo> &columns) = 0;

    /// Signal end of the current row.
    virtual void end_row() = 0;

    /// Finalize and freeze the output data.
    virtual void finish() = 0;

    /// Append a NULL value for column *col*.
    virtual void append_null(size_t col) = 0;

    /// Append an integer value (SQL INT, BIGINT, SMALLINT, TINYINT).
    virtual void append_int64(size_t col, int64_t val) = 0;

    /// Append a floating-point value (SQL FLOAT, DOUBLE, REAL, DECIMAL).
    virtual void append_double(size_t col, double val) = 0;

    /// Append a boolean value (SQL BIT).
    virtual void append_bool(size_t col, bool val) = 0;

    /// Append a string value (SQL VARCHAR, NVARCHAR, etc.).
    virtual void append_string(size_t col, std::string_view val) = 0;
};

} // namespace pygim::core
