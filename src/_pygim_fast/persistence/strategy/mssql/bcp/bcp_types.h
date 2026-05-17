#pragma once
// BCP types: ColumnBinding, BcpContext, null-checking, constexpr temporal converters.
// Simplified from archive: no QuickTimer, no TimingLevel, no TransposeStrategy.

#include "bcp_api.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <arrow/array.h>
#include <arrow/util/bit_util.h>
#include <arrow/util/config.h>   // defines ARROW_VERSION_MAJOR

static_assert(ARROW_VERSION_MAJOR >= 15,
    "pygim requires arrow-cpp >= 15. "
    "Polars 1.x exports strings as StringView (\"vu\" format); "
    "arrow-cpp < 15 rejects this at ImportRecordBatchReader and lacks bind_string_view. "
    "Upgrade: conda install -c conda-forge 'arrow-cpp>=15' 'pyarrow>=15'");

namespace pygim::strategy::mssql::bcp {

// ── ColumnBinding ───────────────────────────────────────────────────────────
/// Per-column metadata populated at bind time and consumed during the row loop.
/// Fields ordered to minimize struct padding (bools packed, pointers grouped).
struct ColumnBinding {
    // ── Fixed-size scalars (4+4 = 8 bytes) ──
    int                           ordinal{0};
    arrow::Type::type             arrow_type{arrow::Type::NA};

    // ── Booleans (4 bytes packed, then DBINT for alignment) ──
    bool            has_nulls{false};
    bool            is_string{false};
    bool            is_binary{false};
    bool            str_buf_bound{false};
    DBINT           last_collen{-2}; // cached bcp_collen value; -2 = unset sentinel

    // ── Cached integer values (8 bytes each, naturally aligned) ──
    int64_t         array_offset{0};
    size_t          value_stride{0};
    size_t          staging_offset{0};

    // ── Raw pointers (8 bytes each) ──
    const uint8_t*  null_bitmap{nullptr};
    const void*     data_ptr{nullptr};
    const int32_t*  offsets32{nullptr};
    const int64_t*  offsets64{nullptr};
    const uint8_t*  str_data{nullptr};
    const arrow::StringViewArray* string_view_array{nullptr};
    const arrow::BinaryViewArray* binary_view_array{nullptr};

    // ── Owning containers (heap-allocated internals) ──
    std::shared_ptr<arrow::Array> array;
    std::vector<uint8_t> str_buf;         // reusable buffer for null-terminated copy

    // Pre-converted buffers (kept alive by shared_ptr)
    std::shared_ptr<std::vector<uint8_t>>                bool_buffer;
    std::shared_ptr<std::vector<int64_t>>                duration_buffer;
    std::shared_ptr<std::vector<SQL_SS_TIME2_STRUCT>>    time_buffer;
    std::shared_ptr<std::vector<SQL_DATE_STRUCT>>        date_buffer;
    std::shared_ptr<std::vector<SQL_TIMESTAMP_STRUCT>>   timestamp_buffer;
};

// ── BcpContext ──────────────────────────────────────────────────────────────
/// Per-BCP-session mutable state: tracks the active ODBC connection, row counts,
/// and flush cadence for one bulk insert session.
struct BcpContext {
    const BcpApi& bcp;
    SQLHDBC       dbc;
    int64_t       batch_size;
    int64_t       sent_rows{0};
    int64_t       rows_until_flush{0};
    int64_t       processed_rows{0};
    int64_t       record_batches{0};

    /// Accumulate row/batch counters from another context.
    /// Session-bound fields (bcp, dbc, batch_size, rows_until_flush) are unchanged.
    BcpContext& operator+=(const BcpContext& rhs) noexcept {
        sent_rows      += rhs.sent_rows;
        processed_rows += rhs.processed_rows;
        record_batches += rhs.record_batches;
        return *this;
    }
};

// ── ClassifiedColumns ───────────────────────────────────────────────────────
struct ClassifiedColumns {
    std::vector<ColumnBinding*> fixed;
    std::vector<ColumnBinding*> string;
    bool any_has_nulls{false};
};

// ── BatchBindingState ───────────────────────────────────────────────────────
/// Cached binding state reused across same-schema RecordBatches.
struct BatchBindingState {
    std::shared_ptr<arrow::Schema> schema;
    std::vector<ColumnBinding>     bindings;
    ClassifiedColumns              classified;
    std::vector<uint8_t>           staging;
    bool                           initialized{false};

    [[nodiscard]] bool matches(const std::shared_ptr<arrow::Schema>& new_schema) const noexcept {
        return initialized && schema && new_schema && schema->Equals(*new_schema);
    }
};

// ── Null check ──────────────────────────────────────────────────────────────
[[nodiscard]] inline bool is_null_at(const ColumnBinding& b, int64_t row) noexcept {
    if (!b.has_nulls) [[likely]] return false;
    if (!b.null_bitmap) return true;
    return !arrow::bit_util::GetBit(b.null_bitmap, b.array_offset + row);
}

// ── Constexpr temporal converters (C++20 chrono) ────────────────────────────

/// Convert Arrow DATE32 (days since epoch) → SQL_DATE_STRUCT.
constexpr SQL_DATE_STRUCT days_to_sql_date(int32_t days_since_epoch) noexcept {
    using namespace std::chrono;
    constexpr sys_days epoch{year{1970}/1/1};
    year_month_day ymd{epoch + days{days_since_epoch}};
    return {
        .year  = static_cast<SQLSMALLINT>(static_cast<int>(ymd.year())),
        .month = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.month())),
        .day   = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.day())),
    };
}

/// Convert Arrow TIME64 (nanoseconds since midnight) → SQL_SS_TIME2_STRUCT.
constexpr SQL_SS_TIME2_STRUCT nanos_to_sql_time(int64_t ns) noexcept {
    constexpr int64_t ns_per_hour   = 3'600'000'000'000LL;
    constexpr int64_t ns_per_minute =    60'000'000'000LL;
    constexpr int64_t ns_per_second =     1'000'000'000LL;
    auto h = static_cast<SQLUSMALLINT>(ns / ns_per_hour);
    ns %= ns_per_hour;
    auto m = static_cast<SQLUSMALLINT>(ns / ns_per_minute);
    ns %= ns_per_minute;
    auto s = static_cast<SQLUSMALLINT>(ns / ns_per_second);
    ns %= ns_per_second;
    auto frac = static_cast<SQLUINTEGER>(ns / 100);
    return SQL_SS_TIME2_STRUCT{
        .hour     = h,
        .minute   = m,
        .second   = s,
        .fraction = frac,
    };
}

/// Convert Arrow TIMESTAMP (µs since epoch) → SQL_TIMESTAMP_STRUCT.
constexpr SQL_TIMESTAMP_STRUCT micros_to_sql_timestamp(int64_t us) noexcept {
    using namespace std::chrono;
    sys_time<microseconds> tp{microseconds{us}};
    auto dp   = floor<days>(tp);
    auto ymd  = year_month_day{dp};
    auto tod  = tp - dp;
    auto h    = duration_cast<hours>(tod);        tod -= h;
    auto m    = duration_cast<minutes>(tod);      tod -= m;
    auto s    = duration_cast<seconds>(tod);      tod -= s;
    auto frac = duration_cast<microseconds>(tod);
    return {
        .year     = static_cast<SQLSMALLINT>(static_cast<int>(ymd.year())),
        .month    = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.month())),
        .day      = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.day())),
        .hour     = static_cast<SQLUSMALLINT>(h.count()),
        .minute   = static_cast<SQLUSMALLINT>(m.count()),
        .second   = static_cast<SQLUSMALLINT>(s.count()),
        .fraction = static_cast<SQLUINTEGER>(frac.count() * 1000),  // nanoseconds
    };
}

} // namespace pygim::strategy::mssql::bcp
