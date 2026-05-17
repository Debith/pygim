// persistence/core/arrow_builder.h
// Columnar builder that materializes ODBC block-cursor results into an Arrow Table.
//
// Backend::LoadImpl creates an ArrowBuilder from the query's arrow::Schema,
// calls append_*() methods per column per fetch block, then finish() to
// produce the final arrow::Table.

#pragma once

#include <arrow/builder.h>
#include <arrow/table.h>
#include <arrow/type.h>
#include <arrow/type_fwd.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pygim::core {

// ────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────

namespace detail {

/// Throw on non-OK Arrow status.  Keeps call-sites terse.
inline void check_arrow(const arrow::Status& s, const char* context) {
    if (!s.ok()) [[unlikely]]
        throw std::runtime_error(std::string(context) + ": " + s.ToString());
}

/// Howard Hinnant's civil_from_days — days since Unix epoch from y/m/d.
/// Public domain algorithm: https://howardhinnant.github.io/date_algorithms.html
constexpr int32_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int      era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

/// Portable temporal structs matching the ODBC wire format.
/// Keeps ArrowBuilder independent of ODBC headers.
struct DateStruct {
    int16_t  year;
    uint16_t month;
    uint16_t day;
};

struct TimestampStruct {
    int16_t  year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint32_t fraction;  // nanoseconds
};

struct Time2Struct {
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint32_t fraction;  // 100-nanosecond units
};

} // namespace detail

// ────────────────────────────────────────────────────────────────
// ArrowBuilder
// ────────────────────────────────────────────────────────────────

/// Columnar batch builder: ODBC block-cursor buffers → arrow::Table.
///
/// Lifecycle:
///   1. LoadImpl creates ArrowBuilder with arrow::Schema from SQLDescribeCol.
///   2. Block cursor fetch loop: call append_*() per column per block.
///   3. finish() finalizes all builders and produces the Table.
class ArrowBuilder {
    std::shared_ptr<arrow::Schema>                    m_schema;
    std::vector<std::unique_ptr<arrow::ArrayBuilder>> m_builders;
    int64_t                                           m_rows{0};
    std::vector<int32_t> m_scratch_i32;   // reusable: date conversion
    std::vector<int64_t> m_scratch_i64;   // reusable: timestamp/time conversion

public:
    /// Construct from schema; one ArrayBuilder per field.
    explicit ArrowBuilder(std::shared_ptr<arrow::Schema> schema,
                          arrow::MemoryPool* pool = arrow::default_memory_pool())
        : m_schema(std::move(schema))
    {
        m_builders.reserve(static_cast<std::size_t>(m_schema->num_fields()));
        for (int i = 0; i < m_schema->num_fields(); ++i) {
            auto result = arrow::MakeBuilder(m_schema->field(i)->type(), pool);
            detail::check_arrow(result.status(), "ArrowBuilder::ctor MakeBuilder");
            m_builders.push_back(std::move(result).ValueUnsafe());
        }
    }

    /// Raw builder access for dispatch table functions.
    [[nodiscard]] arrow::ArrayBuilder* builder_at(std::size_t col) { return m_builders[col].get(); }

    /// Update row count after external append (used by dispatch table).
    void track_rows(std::size_t col) {
        m_rows = std::max(m_rows, m_builders[col]->length());
    }

    // ── Boolean column (SQL_BIT → uint8_t 0/1) ─────────────────

    /// Append boolean values from uint8_t 0/1 bytes (ODBC SQL_BIT format).
    void append_bool(std::size_t col, const uint8_t* values, int64_t count,
                     const uint8_t* valid_bytes = nullptr)
    {
        auto* b = static_cast<arrow::BooleanBuilder*>(m_builders[col].get());
        // Reserve once, then use Unsafe appends to avoid per-element capacity checks.
        detail::check_arrow(b->Reserve(count), "append_bool Reserve");
        for (int64_t i = 0; i < count; ++i) {
            if (valid_bytes && !valid_bytes[i]) {
                b->UnsafeAppendNull();
            } else {
                b->UnsafeAppend(values[i] != 0);
            }
        }
        m_rows = std::max(m_rows, b->length());
    }

    // ── String column ───────────────────────────────────────────

    /// Append strings from a contiguous ODBC column buffer.
    /// Row i starts at `data + i * stride`; `indicators[i]` is byte-length
    /// (>= 0) or negative for NULL (SQL_NULL_DATA).
    void append_strings(std::size_t col, const char* data,
                        const int64_t* indicators,
                        int64_t count, int64_t stride)
    {
        auto* b = static_cast<arrow::StringBuilder*>(m_builders[col].get());
        detail::check_arrow(b->Reserve(count), "append_strings Reserve");
        for (int64_t i = 0; i < count; ++i) {
            if (indicators[i] < 0) {
                b->UnsafeAppendNull();
            } else {
                // Append copies into the builder's buffer — safe even if stride > length.
                detail::check_arrow(
                    b->Append(data + i * stride, static_cast<int32_t>(indicators[i])),
                    "append_strings Append");
            }
        }
        m_rows = std::max(m_rows, b->length());
    }

    // ── Binary column ───────────────────────────────────────────

    /// Append binary data — same layout as strings.
    void append_binary(std::size_t col, const uint8_t* data,
                       const int64_t* indicators,
                       int64_t count, int64_t stride)
    {
        auto* b = static_cast<arrow::BinaryBuilder*>(m_builders[col].get());
        detail::check_arrow(b->Reserve(count), "append_binary Reserve");
        for (int64_t i = 0; i < count; ++i) {
            if (indicators[i] < 0) {
                b->UnsafeAppendNull();
            } else {
                detail::check_arrow(
                    b->Append(data + i * stride, static_cast<int32_t>(indicators[i])),
                    "append_binary Append");
            }
        }
        m_rows = std::max(m_rows, b->length());
    }

    // ── Date column (SQL_DATE_STRUCT → arrow::date32) ───────────

    /// Append dates from SQL_DATE_STRUCT array → days since epoch.
    void append_dates(std::size_t col, const void* date_structs,
                      int64_t count, const uint8_t* valid_bytes = nullptr)
    {
        auto* b        = static_cast<arrow::Date32Builder*>(m_builders[col].get());
        const auto* ds = static_cast<const detail::DateStruct*>(date_structs);

        // Batch-convert to int32_t days, then AppendValues in one shot.
        m_scratch_i32.resize(static_cast<std::size_t>(count));
        for (int64_t i = 0; i < count; ++i) {
            m_scratch_i32[static_cast<std::size_t>(i)] = detail::days_from_civil(
                ds[i].year, ds[i].month, ds[i].day);
        }
        detail::check_arrow(
            b->AppendValues(m_scratch_i32.data(), count, valid_bytes),
            "append_dates AppendValues");

        m_rows = std::max(m_rows, b->length());
    }

    // ── Timestamp column (SQL_TIMESTAMP_STRUCT → µs since epoch) ─

    /// Append timestamps from SQL_TIMESTAMP_STRUCT array → microseconds since epoch.
    void append_timestamps(std::size_t col, const void* ts_structs,
                           int64_t count, const uint8_t* valid_bytes = nullptr)
    {
        auto* b        = static_cast<arrow::TimestampBuilder*>(m_builders[col].get());
        const auto* ts = static_cast<const detail::TimestampStruct*>(ts_structs);

        m_scratch_i64.resize(static_cast<std::size_t>(count));
        for (int64_t i = 0; i < count; ++i) {
            int64_t day_us = static_cast<int64_t>(detail::days_from_civil(
                ts[i].year, ts[i].month, ts[i].day)) * 86'400'000'000LL;
            int64_t time_us = static_cast<int64_t>(ts[i].hour)   * 3'600'000'000LL
                            + static_cast<int64_t>(ts[i].minute) *    60'000'000LL
                            + static_cast<int64_t>(ts[i].second) *     1'000'000LL
                            // ODBC: fraction is in nanoseconds
                            + static_cast<int64_t>(ts[i].fraction) / 1000;
            m_scratch_i64[static_cast<std::size_t>(i)] = day_us + time_us;
        }
        detail::check_arrow(
            b->AppendValues(m_scratch_i64.data(), count, valid_bytes),
            "append_timestamps AppendValues");

        m_rows = std::max(m_rows, b->length());
    }

    // ── Time column (SQL_SS_TIME2_STRUCT → µs since midnight) ───

    /// Append times from SQL_SS_TIME2_STRUCT array → microseconds since midnight.
    /// SQL Server extended TIME type with fractional second precision.
    /// fraction field is in 100-nanosecond units.
    void append_times_ext(std::size_t col, const void* time_structs,
                          int64_t count, const uint8_t* valid_bytes = nullptr)
    {
        auto* b         = static_cast<arrow::Time64Builder*>(m_builders[col].get());
        const auto* tms = static_cast<const detail::Time2Struct*>(time_structs);

        m_scratch_i64.resize(static_cast<std::size_t>(count));
        for (int64_t i = 0; i < count; ++i) {
            m_scratch_i64[static_cast<std::size_t>(i)] =
                  static_cast<int64_t>(tms[i].hour)     * 3'600'000'000LL
                + static_cast<int64_t>(tms[i].minute)   *    60'000'000LL
                + static_cast<int64_t>(tms[i].second)   *     1'000'000LL
                + static_cast<int64_t>(tms[i].fraction) / 10;  // 100ns → µs
        }
        detail::check_arrow(
            b->AppendValues(m_scratch_i64.data(), count, valid_bytes),
            "append_times_ext AppendValues");

        m_rows = std::max(m_rows, b->length());
    }

    // ── Finalize ────────────────────────────────────────────────

    /// Finalize all builders and produce the result Table.
    [[nodiscard]] std::shared_ptr<arrow::Table> finish()
    {
        std::vector<std::shared_ptr<arrow::Array>> columns;
        columns.reserve(m_builders.size());

        for (auto& builder : m_builders) {
            auto result = builder->Finish();
            detail::check_arrow(result.status(), "ArrowBuilder::finish");
            columns.push_back(std::move(result).ValueUnsafe());
        }

        return arrow::Table::Make(m_schema, std::move(columns));
    }

    // ── Accessors ───────────────────────────────────────────────

    [[nodiscard]] int64_t     row_count()   const { return m_rows; }
    [[nodiscard]] std::size_t num_columns() const { return m_builders.size(); }
};

} // namespace pygim::core
