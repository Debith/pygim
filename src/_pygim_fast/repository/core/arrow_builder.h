// repository/core/arrow_builder.h
// Columnar builder that materializes ODBC fetch results into Arrow RecordBatches.
//
// Backend::LoadImpl creates an ArrowBuilder with column schema from SQLDescribeCol,
// calls append_*() methods per column per fetch block, then finish() to produce
// the final RecordBatch. Placeholder: logs operations without producing real arrays.

#pragma once

#include "../../utils/logging.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::core {

// ────────────────────────────────────────────────────────────────
// Column schema placeholder
// ────────────────────────────────────────────────────────────────

/// Column data types supported by ArrowBuilder.
/// Maps to Arrow logical types: Int64→arrow::int64(), Double→arrow::float64(),
/// Bool→arrow::boolean(), String→arrow::utf8().
enum class ColumnType { Int64, Double, Bool, String };

struct ColumnInfo {
    std::string name;
    ColumnType  type;
};

// ────────────────────────────────────────────────────────────────
// ArrowBuilder — placeholder
// ────────────────────────────────────────────────────────────────

/// ArrowBuilder — Columnar batch builder for ODBC result sets → Arrow RecordBatch.
///
/// Lifecycle:
///   1. LoadImpl creates ArrowBuilder with column schema from SQLDescribeCol.
///   2. Block cursor fetch loop: append_*_batch() per column, advance_rows() per block.
///   3. finish() finalizes the RecordBatch.
///
/// Placeholder: currently logs operations; real implementation will use
/// arrow::ArrayBuilder per column.
class ArrowBuilder {
    std::vector<ColumnInfo> m_columns;
    std::size_t             m_row_count = 0;

public:
    explicit ArrowBuilder(std::vector<ColumnInfo> columns)
        : m_columns(std::move(columns))
    {
        PYGIM_LOG_FMT("[ArrowBuilder] created with %zu columns:", m_columns.size());
        for (auto const& c : m_columns)
            PYGIM_LOG_FMT(" %s", c.name.c_str());
        PYGIM_LOG_FMT("\n");
    }

    void append_int64(std::size_t col, std::span<const int64_t> values,
                      std::span<const uint8_t> valid) {
        PYGIM_LOG_FMT("[ArrowBuilder] append_int64(col=%zu, count=%zu)\n",
                      col, values.size());
        m_row_count += values.size();
    }

    void append_double(std::size_t col, std::span<const double> values,
                       std::span<const uint8_t> valid) {
        PYGIM_LOG_FMT("[ArrowBuilder] append_double(col=%zu, count=%zu)\n",
                      col, values.size());
    }

    void append_bool(std::size_t col, bool value) {
        PYGIM_LOG_FMT("[ArrowBuilder] append_bool(col=%zu)\n", col);
    }

    void append_string(std::size_t col, std::string_view value) {
        PYGIM_LOG_FMT("[ArrowBuilder] append_string(col=%zu, \"%.*s\")\n",
                      col, static_cast<int>(value.size()), value.data());
    }

    void advance_rows(std::size_t nrows) {
        m_row_count += nrows;
        PYGIM_LOG_FMT("[ArrowBuilder] advance_rows(%zu) \xe2\x86\x92 total %zu\n",
                      nrows, m_row_count);
    }

    // Placeholder: in real code returns arrow::RecordBatch
    void finish() {
        PYGIM_LOG_FMT("[ArrowBuilder] finish() \xe2\x86\x92 RecordBatch with %zu rows, "
                      "%zu columns\n", m_row_count, m_columns.size());
    }

    [[nodiscard]] std::size_t row_count() const { return m_row_count; }
    [[nodiscard]] auto const& columns() const { return m_columns; }
};

} // namespace pygim::core
