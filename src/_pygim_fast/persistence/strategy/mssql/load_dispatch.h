// repository/strategy/mssql/load_dispatch.h
// Compile-time dispatch table for the load pipeline's block-append loop.
// Mirrors bcp_bind_dispatch.h pattern: O(1) branch-free type routing.

#pragma once

#include "fetch_buffer.h"
#include "../../core/arrow_builder.h"

#include <arrow/builder.h>
#include <arrow/type.h>
#include <array>
#include <cstdint>
#include <vector>

namespace pygim::strategy::mssql {

// ── Function pointer type ───────────────────────────────────────
/// Append one column's block of fetched data to the Arrow builder.
/// @param builder   ArrowBuilder holding typed builders per column.
/// @param col       Column index.
/// @param buf       FetchBuffer with raw ODBC data + indicators.
/// @param nrows     Number of rows in this block.
/// @param valid     Validity bytes (nullptr if column is non-nullable).
using BlockAppendFn = void (*)(core::ArrowBuilder& builder,
                               std::size_t col,
                               const FetchBuffer& buf,
                               int64_t nrows,
                               const uint8_t* valid);

// ── Per-column dispatch info ────────────────────────────────────
/// Pre-computed per-column: function pointer + nullable flag.
/// Built once after SQLDescribeCol, used in every fetch block.
struct ColumnDispatch {
    BlockAppendFn fn{nullptr};
    bool          nullable{true};  // from SQLDescribeCol
};

// ── Per-type append functions ───────────────────────────────────

namespace detail {

using core::detail::check_arrow;

// ── Fixed-width types ───────────────────────────────────────────
// Each calls the typed arrow::ArrayBuilder directly via builder_at(col),
// bypassing ArrowBuilder's internal switch.

inline void append_block_int8(core::ArrowBuilder& b, std::size_t c,
                              const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::Int8Builder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(
        reinterpret_cast<const int8_t*>(buf.data.data()), n, v), "int8");
    b.track_rows(c);
}

inline void append_block_int16(core::ArrowBuilder& b, std::size_t c,
                               const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::Int16Builder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(
        reinterpret_cast<const int16_t*>(buf.data.data()), n, v), "int16");
    b.track_rows(c);
}

inline void append_block_int32(core::ArrowBuilder& b, std::size_t c,
                               const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::Int32Builder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(
        reinterpret_cast<const int32_t*>(buf.data.data()), n, v), "int32");
    b.track_rows(c);
}

inline void append_block_int64(core::ArrowBuilder& b, std::size_t c,
                               const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::Int64Builder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(
        reinterpret_cast<const int64_t*>(buf.data.data()), n, v), "int64");
    b.track_rows(c);
}

inline void append_block_float(core::ArrowBuilder& b, std::size_t c,
                               const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::FloatBuilder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(
        reinterpret_cast<const float*>(buf.data.data()), n, v), "float");
    b.track_rows(c);
}

inline void append_block_double(core::ArrowBuilder& b, std::size_t c,
                                const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::DoubleBuilder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(
        reinterpret_cast<const double*>(buf.data.data()), n, v), "double");
    b.track_rows(c);
}

inline void append_block_fixed_binary(core::ArrowBuilder& b, std::size_t c,
                                      const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    auto* builder = static_cast<arrow::FixedSizeBinaryBuilder*>(b.builder_at(c));
    check_arrow(builder->AppendValues(buf.data.data(), n, v), "fixed_binary");
    b.track_rows(c);
}

// ── Boolean ─────────────────────────────────────────────────────
inline void append_block_bool(core::ArrowBuilder& b, std::size_t c,
                              const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    // Delegate to ArrowBuilder's bool method (Reserve + UnsafeAppend loop)
    b.append_bool(c, buf.data.data(), n, v);
}

// ── String ──────────────────────────────────────────────────────
inline void append_block_string(core::ArrowBuilder& b, std::size_t c,
                                const FetchBuffer& buf, int64_t n, const uint8_t* /*v*/) {
    // String uses indicators for both null detection and length — ignores valid_bytes
    b.append_strings(c,
        reinterpret_cast<const char*>(buf.data.data()),
        reinterpret_cast<const int64_t*>(buf.indicators.data()),
        n, buf.mapping.element_width);
}

// ── Binary ──────────────────────────────────────────────────────
inline void append_block_binary(core::ArrowBuilder& b, std::size_t c,
                                const FetchBuffer& buf, int64_t n, const uint8_t* /*v*/) {
    b.append_binary(c,
        buf.data.data(),
        reinterpret_cast<const int64_t*>(buf.indicators.data()),
        n, buf.mapping.element_width);
}

// ── Temporal ────────────────────────────────────────────────────
inline void append_block_date32(core::ArrowBuilder& b, std::size_t c,
                                const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    b.append_dates(c, buf.data.data(), n, v);
}

inline void append_block_timestamp(core::ArrowBuilder& b, std::size_t c,
                                   const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    b.append_timestamps(c, buf.data.data(), n, v);
}

inline void append_block_time64(core::ArrowBuilder& b, std::size_t c,
                                const FetchBuffer& buf, int64_t n, const uint8_t* v) {
    b.append_times_ext(c, buf.data.data(), n, v);
}

} // namespace detail

// ── Compile-time dispatch table ─────────────────────────────────
/// arrow::Type::type → block append function.
/// Same pattern as bcp_bind_dispatch.h. O(1) branch-free at runtime.
inline constexpr auto append_dispatch = []() consteval {
    std::array<BlockAppendFn, arrow::Type::MAX_ID> t{};
    t[arrow::Type::INT8]              = &detail::append_block_int8;
    t[arrow::Type::INT16]             = &detail::append_block_int16;
    t[arrow::Type::INT32]             = &detail::append_block_int32;
    t[arrow::Type::INT64]             = &detail::append_block_int64;
    t[arrow::Type::FLOAT]             = &detail::append_block_float;
    t[arrow::Type::DOUBLE]            = &detail::append_block_double;
    t[arrow::Type::BOOL]              = &detail::append_block_bool;
    t[arrow::Type::FIXED_SIZE_BINARY] = &detail::append_block_fixed_binary;
    t[arrow::Type::STRING]            = &detail::append_block_string;
    t[arrow::Type::BINARY]            = &detail::append_block_binary;
    t[arrow::Type::DATE32]            = &detail::append_block_date32;
    t[arrow::Type::TIMESTAMP]         = &detail::append_block_timestamp;
    t[arrow::Type::TIME64]            = &detail::append_block_time64;
    return t;
}();

/// Build per-column dispatch info after SQLDescribeCol.
/// Resolves function pointer once; used every fetch block.
[[nodiscard]]
inline std::vector<ColumnDispatch>
build_column_dispatch(const std::shared_ptr<arrow::Schema>& schema,
                      const std::vector<bool>& nullable_flags) {
    std::vector<ColumnDispatch> result;
    result.reserve(static_cast<std::size_t>(schema->num_fields()));

    for (int i = 0; i < schema->num_fields(); ++i) {
        auto type_id = schema->field(i)->type()->id();
        auto idx = static_cast<std::size_t>(type_id);

        BlockAppendFn fn = (idx < append_dispatch.size()) ? append_dispatch[idx] : nullptr;
        if (!fn) [[unlikely]] {
            throw std::runtime_error(
                "load_dispatch: unsupported Arrow type "
                + std::to_string(static_cast<int>(type_id))
                + " for column " + schema->field(i)->name());
        }

        result.push_back(ColumnDispatch{
            .fn = fn,
            .nullable = nullable_flags[static_cast<std::size_t>(i)],
        });
    }
    return result;
}

// Verify portable temporal structs match ODBC wire format.
static_assert(sizeof(core::detail::DateStruct)      == sizeof(SQL_DATE_STRUCT));
static_assert(sizeof(core::detail::TimestampStruct)  == sizeof(SQL_TIMESTAMP_STRUCT));
static_assert(sizeof(core::detail::Time2Struct)       == sizeof(SQL_SS_TIME2_STRUCT));

} // namespace pygim::strategy::mssql
