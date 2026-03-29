// repository/core/arrow_builder.h
// Core C++ package — ArrowBuilder placeholder.
//
// The one and only builder in core.  Concrete class, no virtuals.
// Backend::LoadImpl drives this via block cursor batches.
// finish() returns arrow::RecordBatch (placeholder: prints and returns empty).

#pragma once

#include "../../utils/logging.h"
#include <string>
#include <string_view>
#include <vector>

namespace pygim::core {

// ────────────────────────────────────────────────────────────────
// Column schema placeholder
// ────────────────────────────────────────────────────────────────

enum class ColumnType { Int64, Double, Bool, String };

struct ColumnInfo {
    std::string name;
    ColumnType  type;
};

// ────────────────────────────────────────────────────────────────
// ArrowBuilder — placeholder
// ────────────────────────────────────────────────────────────────

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

    void append_int64(std::size_t col, const int64_t* values,
                      const uint8_t* valid, std::size_t count) {
        PYGIM_LOG_FMT("[ArrowBuilder] append_int64(col=%zu, count=%zu)\n",
                      col, count);
        m_row_count += count;
    }

    void append_double(std::size_t col, const double* values,
                       const uint8_t* valid, std::size_t count) {
        PYGIM_LOG_FMT("[ArrowBuilder] append_double(col=%zu, count=%zu)\n",
                      col, count);
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
