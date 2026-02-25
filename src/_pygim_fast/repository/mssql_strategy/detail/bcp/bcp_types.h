#pragma once
// BCP types: ColumnBinding, BcpContext, null-checking, constexpr temporal converters.

#include "bcp_api.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <arrow/array.h>
#include <arrow/util/bit_util.h>

#if defined(ARROW_VERSION_MAJOR) && (ARROW_VERSION_MAJOR >= 15)
#define PYGIM_HAVE_ARROW_STRING_VIEW 1
#else
#define PYGIM_HAVE_ARROW_STRING_VIEW 0
#endif

// Forward-declare to avoid pulling in the full header in every translation unit.
namespace pygim { class QuickTimer; }

namespace pygim::bcp {

// ── ColumnBinding ───────────────────────────────────────────────────────────
/// Per-column metadata populated at bind time and consumed during the row loop.
struct ColumnBinding {
    int                           ordinal{0};
    arrow::Type::type             arrow_type{arrow::Type::NA};
    std::shared_ptr<arrow::Array> array;

    bool            has_nulls{false};
    bool            is_string{false};
    const uint8_t*  null_bitmap{nullptr};
    int64_t         array_offset{0};

    // Fixed-width column data (direct pointer into Arrow buffer)
    const void* data_ptr{nullptr};
    size_t      value_stride{0};
    size_t      staging_offset{0};

    // String column data (zero-copy from Arrow buffers)
    const int32_t* offsets32{nullptr};
    const int64_t* offsets64{nullptr};
    const uint8_t* str_data{nullptr};
#if PYGIM_HAVE_ARROW_STRING_VIEW
    const arrow::StringViewArray* string_view_array{nullptr};
#endif
    std::vector<uint8_t> str_buf;        // reusable buffer for null-terminated copy
    bool                 str_buf_bound{false};

    // Pre-converted temporal buffers (kept alive by shared_ptr)
    std::shared_ptr<std::vector<SQL_DATE_STRUCT>>      date_buffer;
    std::shared_ptr<std::vector<SQL_TIMESTAMP_STRUCT>>  timestamp_buffer;
};

// ── BcpContext ──────────────────────────────────────────────────────────────
/// Session state threaded through all BCP helper functions.
struct BcpContext {
    const BcpApi& bcp;
    SQLHDBC       dbc;
    QuickTimer&   timer;
    int64_t       batch_size;
    int64_t       sent_rows{0};
    int64_t       processed_rows{0};
    int64_t       record_batches{0};
};

// ── ClassifiedColumns ───────────────────────────────────────────────────────
struct ClassifiedColumns {
    std::vector<ColumnBinding*> fixed;
    std::vector<ColumnBinding*> string;
    bool any_has_nulls{false};
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

} // namespace pygim::bcp
