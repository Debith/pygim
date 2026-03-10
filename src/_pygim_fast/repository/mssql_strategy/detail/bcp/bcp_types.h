#pragma once
// BCP types: ColumnBinding, BcpContext, null-checking, constexpr temporal converters.

#include "bcp_api.h"
#include "../../../../utils/quick_timer.h"

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

#if ARROW_VERSION_MAJOR >= 15
#define PYGIM_HAVE_ARROW_STRING_VIEW 1
#else
static_assert(false,
    "pygim requires arrow-cpp >= 15. "
    "Polars 1.x exports strings as StringView (\"vu\" format); "
    "arrow-cpp < 15 rejects this at ImportRecordBatchReader and lacks bind_string_view. "
    "Upgrade: conda install -c conda-forge 'arrow-cpp>=15' 'pyarrow>=15'");
#endif

namespace pygim::bcp {

enum class TimingLevel {
    Off,
    Stage,
    Hot,
};

[[nodiscard]] inline const char* timing_level_name(TimingLevel level) noexcept {
    switch (level) {
        case TimingLevel::Off:   return "off";
        case TimingLevel::Stage: return "stage";
        case TimingLevel::Hot:   return "hot";
    }
    return "stage";
}

// Forward-declare so BcpContext can hold a non-owning pointer.
struct TransposeStrategy;

// ── ColumnBinding ───────────────────────────────────────────────────────────
/// Per-column metadata populated at bind time and consumed during the row loop.
struct ColumnBinding {
    int                           ordinal{0};
    arrow::Type::type             arrow_type{arrow::Type::NA};
    std::shared_ptr<arrow::Array> array;

    bool            has_nulls{false};
    bool            is_string{false};
    bool            is_binary{false};   // binary columns use a length-prefix, not terminator
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
    const arrow::BinaryViewArray* binary_view_array{nullptr};
#endif
    std::vector<uint8_t> str_buf;        // reusable buffer for null-terminated copy
    bool                 str_buf_bound{false};

    // Pre-converted buffers (kept alive by shared_ptr)
    std::shared_ptr<std::vector<uint8_t>>                bool_buffer;
    std::shared_ptr<std::vector<int64_t>>                duration_buffer;
    std::shared_ptr<std::vector<SQL_SS_TIME2_STRUCT>>    time_buffer;
    std::shared_ptr<std::vector<SQL_DATE_STRUCT>>        date_buffer;
    std::shared_ptr<std::vector<SQL_TIMESTAMP_STRUCT>>   timestamp_buffer;
};

// ── BcpContext ──────────────────────────────────────────────────────────────
/// Session state threaded through all BCP helper functions.
struct BcpContext {
    static constexpr std::size_t kInvalidTimerId = static_cast<std::size_t>(-1);

    const BcpApi& bcp;
    SQLHDBC       dbc;
    QuickTimer&   timer;
    int64_t       batch_size;
    int64_t       sent_rows{0};
    int64_t       rows_until_flush{0};  // decrementing counter; reset to batch_size on flush
    int64_t       processed_rows{0};
    int64_t       record_batches{0};

    // Micro-metrics for row-loop internals (seconds)
    double fixed_copy_seconds{0.0};
    double colptr_redirect_seconds{0.0};
    double string_pack_seconds{0.0};
    double sendrow_seconds{0.0};
    std::string simd_level{"scalar"};
    TimingLevel timing_level{TimingLevel::Stage};

    // Pre-resolved QuickTimer indices (avoid repeated name-based lookups)
    std::size_t timer_row_loop_id{kInvalidTimerId};
    std::size_t timer_bind_columns_id{kInvalidTimerId};
    std::size_t timer_batch_flush_id{kInvalidTimerId};
    std::size_t timer_setup_id{kInvalidTimerId};
    std::size_t timer_done_id{kInvalidTimerId};

    /// Pluggable transpose strategy (non-owning).  When nullptr, falls back
    /// to the default RowMajorTranspose defined in bcp_transpose_strategy.h.
    TransposeStrategy* transpose{nullptr};
};

[[nodiscard]] inline bool stage_timing_enabled(const BcpContext& ctx) noexcept {
    return ctx.timing_level != TimingLevel::Off;
}

[[nodiscard]] inline bool hot_timing_enabled(const BcpContext& ctx) noexcept {
    return ctx.timing_level == TimingLevel::Hot;
}

[[nodiscard]] inline std::chrono::steady_clock::time_point hot_timer_start(
    const BcpContext& ctx) noexcept {
    // Only collect per-row micro-metrics at Hot level.  At Stage level, the
    // coarse sub-timers (row_loop, batch_flush, etc.) already capture overall
    // timing outside the loop.  Calling steady_clock::now() 6× per row in the
    // default Stage mode was measured at ~100 ms / 1 M rows (3–5% overhead).
    if (hot_timing_enabled(ctx)) return std::chrono::steady_clock::now();
    return {};
}

inline void hot_timer_add(const BcpContext& ctx,
                          std::chrono::steady_clock::time_point t0,
                          double& accumulator) noexcept {
    if (!hot_timing_enabled(ctx)) return;
    accumulator += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
}

inline void stage_timer_start(BcpContext& ctx,
                              std::size_t timer_id,
                              const char* fallback_name) {
    if (!stage_timing_enabled(ctx)) return;
    if (timer_id != BcpContext::kInvalidTimerId)
        ctx.timer.start_sub_timer(timer_id, false);
    else
        ctx.timer.start_sub_timer(fallback_name, false);
}

inline void stage_timer_stop(BcpContext& ctx,
                             std::size_t timer_id,
                             const char* fallback_name) {
    if (!stage_timing_enabled(ctx)) return;
    if (timer_id != BcpContext::kInvalidTimerId)
        ctx.timer.stop_sub_timer(timer_id, false);
    else
        ctx.timer.stop_sub_timer(fallback_name, false);
}

// ── ClassifiedColumns ───────────────────────────────────────────────────────
struct ClassifiedColumns {
    std::vector<ColumnBinding*> fixed;
    std::vector<ColumnBinding*> string;
    bool any_has_nulls{false};
};

// ── BatchBindingState ───────────────────────────────────────────────────────
/// Cached binding state that can be reused across same-schema RecordBatches.
/// Eliminates repeated bcp_bind / bcp_colptr / bcp_collen calls when the
/// schema is identical between batches (the common case in streaming loads).
struct BatchBindingState {
    std::shared_ptr<arrow::Schema> schema;       ///< Schema of the last bound batch
    std::vector<ColumnBinding>     bindings;      ///< Column bindings (reused across batches)
    ClassifiedColumns              classified;    ///< Fixed/string classification
    std::vector<uint8_t>           staging;       ///< Staging buffer (stable address for bcp_colptr)
    bool                           initialized{false};

    /// Check whether this state can be reused for a new batch.
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
    // fraction is in 100-nanosecond units for SQL Server TIME(7)
    auto frac = static_cast<SQLUINTEGER>(ns / 100);
    SQL_SS_TIME2_STRUCT t{};
    t.hour     = h;
    t.minute   = m;
    t.second   = s;
    t.fraction = frac;
    return t;
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
