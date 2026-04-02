#pragma once
// BCP bind dispatch: per-type bind functions and compile-time dispatch table.
// Each function creates a ColumnBinding for a specific Arrow type by delegating
// to make_fixed_binding / make_string_binding / make_binary_binding.

#include "bcp_types.h"
#include "../odbc_error.h"

#include <array>
#include <arrow/table.h>

namespace pygim::strategy::mssql::bcp {

// ── Generic fixed-width binding ─────────────────────────────────────────────
/// Create a fixed-width BCP column binding.
/// Passes the Arrow data pointer directly to bcp_bind (zero-copy for numeric types).
/// @param data    Raw data pointer from Arrow array (e.g. Int32Array::raw_values()).
/// @param stride  Byte width per element (sizeof the C type).
/// @param bcp_type  BCP type token from sql_type:: namespace.
/// @throws std::runtime_error if bcp_bind fails.
inline ColumnBinding make_fixed_binding(
    const BcpApi& bcp, SQLHDBC dbc,
    const void* data, size_t stride, int bcp_type,
    const std::shared_ptr<arrow::Array>& array, int ordinal)
{
    ColumnBinding b{};
    b.ordinal      = ordinal;
    b.arrow_type   = array->type_id();
    b.array        = array;
    b.has_nulls    = array->null_count() > 0;
    b.null_bitmap  = array->null_bitmap_data();
    b.array_offset = array->offset();
    b.data_ptr     = data;
    b.value_stride = stride;

    auto ret = bcp.bind(dbc, reinterpret_cast<LPCBYTE>(data),
                        0, static_cast<DBINT>(stride), nullptr, 0, bcp_type, ordinal);
    if (ret != kSucceed)
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_bind");
    return b;
}

// ── Generic string binding ──────────────────────────────────────────────────
/// Create a variable-length string BCP column binding.
/// Uses a static dummy pointer at bind time because bcp_bind for variable-length
/// columns requires a non-null address; actual data is redirected per-row via
/// bcp_colptr in handle_string_column.
inline ColumnBinding make_string_binding(
    const BcpApi& bcp, SQLHDBC dbc,
    const std::shared_ptr<arrow::Array>& array, int ordinal)
{
    static const uint8_t dummy = 0;
    static const uint8_t term  = 0;

    ColumnBinding b{};
    b.ordinal      = ordinal;
    b.arrow_type   = array->type_id();
    b.array        = array;
    b.has_nulls    = array->null_count() > 0;
    b.null_bitmap  = array->null_bitmap_data();
    b.array_offset = array->offset();
    b.is_string    = true;
    b.str_buf.resize(256);
    b.last_collen  = -2;  // sentinel: never matches a real length

    auto ret = bcp.bind(dbc, reinterpret_cast<LPCBYTE>(&dummy),
                        0, sql_type::varlen_data,
                        reinterpret_cast<LPCBYTE>(&term),
                        1, sql_type::character, ordinal);
    if (ret != kSucceed)
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_bind");
    return b;
}

// ── Generic binary binding ──────────────────────────────────────────────────

inline ColumnBinding make_binary_binding(
    const BcpApi& bcp, SQLHDBC dbc,
    const std::shared_ptr<arrow::Array>& array, int ordinal)
{
    static const uint8_t dummy = 0;

    ColumnBinding b{};
    b.ordinal      = ordinal;
    b.arrow_type   = array->type_id();
    b.array        = array;
    b.has_nulls    = array->null_count() > 0;
    b.null_bitmap  = array->null_bitmap_data();
    b.array_offset = array->offset();
    b.is_string    = true;   // reuse variable-length row loop path
    b.is_binary    = true;
    b.str_buf.resize(256 + sizeof(DBINT));

    auto ret = bcp.bind(dbc, reinterpret_cast<LPCBYTE>(&dummy),
                        static_cast<int>(sizeof(DBINT)),  // 4-byte prefix
                        sql_type::varlen_data,
                        nullptr, 0,
                        sql_type::binary, ordinal);
    if (ret != kSucceed)
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_bind");
    return b;
}

// ── Per-type binding functions ──────────────────────────────────────────────

inline ColumnBinding bind_int8(const BcpApi& bcp, SQLHDBC dbc,
                               const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::Int8Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(int8_t), sql_type::int1, a, ord);
}

inline ColumnBinding bind_int16(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::Int16Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(int16_t), sql_type::int2, a, ord);
}

inline ColumnBinding bind_int32(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::Int32Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(int32_t), sql_type::int4, a, ord);
}

inline ColumnBinding bind_int64(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::Int64Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(int64_t), sql_type::bigint, a, ord);
}

inline ColumnBinding bind_uint8(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::UInt8Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(uint8_t), sql_type::int1, a, ord);
}

inline ColumnBinding bind_uint16(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::UInt16Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(uint16_t), sql_type::int2, a, ord);
}

inline ColumnBinding bind_uint32(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::UInt32Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(uint32_t), sql_type::int4, a, ord);
}

inline ColumnBinding bind_uint64(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::UInt64Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(uint64_t), sql_type::bigint, a, ord);
}

inline ColumnBinding bind_bool(const BcpApi& bcp, SQLHDBC dbc,
                               const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::BooleanArray>(a);
    const auto n = typed->length();
    auto buf = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(n));
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*buf)[static_cast<size_t>(i)] = typed->Value(i) ? 1 : 0;
    }
    auto b = make_fixed_binding(bcp, dbc, buf->data(), sizeof(uint8_t),
                                sql_type::bit, a, ord);
    b.bool_buffer = std::move(buf);
    return b;
}

inline ColumnBinding bind_float(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::FloatArray>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(float), sql_type::flt4, a, ord);
}

inline ColumnBinding bind_double(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::DoubleArray>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(double), sql_type::flt8, a, ord);
}

inline ColumnBinding bind_date32(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::Date32Array>(a);
    const auto n = typed->length();
    auto buf = std::make_shared<std::vector<SQL_DATE_STRUCT>>(static_cast<size_t>(n));
    const auto* raw = typed->raw_values();
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*buf)[static_cast<size_t>(i)] = days_to_sql_date(raw[i]);
    }
    auto b = make_fixed_binding(bcp, dbc, buf->data(), sizeof(SQL_DATE_STRUCT),
                                sql_type::daten, a, ord);
    b.date_buffer = std::move(buf);
    return b;
}

inline ColumnBinding bind_timestamp(const BcpApi& bcp, SQLHDBC dbc,
                                    const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::TimestampArray>(a);
    const auto n = typed->length();
    auto buf = std::make_shared<std::vector<SQL_TIMESTAMP_STRUCT>>(static_cast<size_t>(n));
    const auto* raw = typed->raw_values();
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*buf)[static_cast<size_t>(i)] = micros_to_sql_timestamp(raw[i]);
    }
    auto b = make_fixed_binding(bcp, dbc, buf->data(), sizeof(SQL_TIMESTAMP_STRUCT),
                                sql_type::datetime2n, a, ord);
    b.timestamp_buffer = std::move(buf);
    return b;
}

inline ColumnBinding bind_time64(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::Time64Array>(a);
    const auto n = typed->length();
    auto buf = std::make_shared<std::vector<SQL_SS_TIME2_STRUCT>>(static_cast<size_t>(n));
    const auto* raw = typed->raw_values();
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*buf)[static_cast<size_t>(i)] = nanos_to_sql_time(raw[i]);
    }
    auto b = make_fixed_binding(bcp, dbc, buf->data(), sizeof(SQL_SS_TIME2_STRUCT),
                                sql_type::timen, a, ord);
    b.time_buffer = std::move(buf);
    return b;
}

inline ColumnBinding bind_duration(const BcpApi& bcp, SQLHDBC dbc,
                                   const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::DurationArray>(a);
    return make_fixed_binding(bcp, dbc, typed->raw_values(), sizeof(int64_t), sql_type::bigint, a, ord);
}

inline ColumnBinding bind_large_binary(const BcpApi& bcp, SQLHDBC dbc,
                                       const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::LargeBinaryArray>(a);
    auto b = make_binary_binding(bcp, dbc, a, ord);
    b.offsets64 = typed->raw_value_offsets();
    b.str_data  = typed->value_data()->data();
    return b;
}

inline ColumnBinding bind_string(const BcpApi& bcp, SQLHDBC dbc,
                                 const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::StringArray>(a);
    auto b = make_string_binding(bcp, dbc, a, ord);
    b.offsets32 = typed->raw_value_offsets();
    b.str_data  = typed->value_data()->data();
    return b;
}

inline ColumnBinding bind_large_string(const BcpApi& bcp, SQLHDBC dbc,
                                       const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::LargeStringArray>(a);
    auto b = make_string_binding(bcp, dbc, a, ord);
    b.offsets64 = typed->raw_value_offsets();
    b.str_data  = typed->value_data()->data();
    return b;
}

inline ColumnBinding bind_string_view(const BcpApi& bcp, SQLHDBC dbc,
                                      const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::StringViewArray>(a);
    auto b = make_string_binding(bcp, dbc, a, ord);
    b.string_view_array = typed.get();
    return b;
}

inline ColumnBinding bind_binary_view(const BcpApi& bcp, SQLHDBC dbc,
                                      const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::BinaryViewArray>(a);
    auto b = make_binary_binding(bcp, dbc, a, ord);
    b.binary_view_array = typed.get();
    return b;
}

// ── Dispatch table ──────────────────────────────────────────────────────────
// Compile-time function-pointer table indexed by arrow::Type::type.
// Inspired by GBA Thumb decoder's consteval dispatch: pay the categorisation
// cost once at compile time, get O(1) branch-free dispatch at runtime.

/// Signature for per-type bind functions.
using BindFn = ColumnBinding(*)(const BcpApi&, SQLHDBC,
                                const std::shared_ptr<arrow::Array>&, int);

/// Compile-time dispatch table: Arrow type id → bind function.
/// Unsupported types map to nullptr; checked at call site.
inline constexpr auto bind_dispatch = []() consteval {
    std::array<BindFn, arrow::Type::MAX_ID> t{};
    t[arrow::Type::INT8]         = &bind_int8;
    t[arrow::Type::INT16]        = &bind_int16;
    t[arrow::Type::INT32]        = &bind_int32;
    t[arrow::Type::INT64]        = &bind_int64;
    t[arrow::Type::UINT8]        = &bind_uint8;
    t[arrow::Type::UINT16]       = &bind_uint16;
    t[arrow::Type::UINT32]       = &bind_uint32;
    t[arrow::Type::UINT64]       = &bind_uint64;
    t[arrow::Type::BOOL]         = &bind_bool;
    t[arrow::Type::FLOAT]        = &bind_float;
    t[arrow::Type::DOUBLE]       = &bind_double;
    t[arrow::Type::DATE32]       = &bind_date32;
    t[arrow::Type::TIMESTAMP]    = &bind_timestamp;
    t[arrow::Type::TIME64]       = &bind_time64;
    t[arrow::Type::DURATION]     = &bind_duration;
    t[arrow::Type::STRING]       = &bind_string;
    t[arrow::Type::LARGE_STRING] = &bind_large_string;
    t[arrow::Type::LARGE_BINARY] = &bind_large_binary;
    t[arrow::Type::STRING_VIEW]  = &bind_string_view;
    t[arrow::Type::BINARY_VIEW]  = &bind_binary_view;
    return t;
}();

} // namespace pygim::strategy::mssql::bcp
