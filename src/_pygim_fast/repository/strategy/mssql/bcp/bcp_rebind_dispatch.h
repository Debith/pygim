#pragma once
// BCP rebind dispatch: per-type update functions and compile-time dispatch table.
// Used to update Arrow data pointers in existing ColumnBindings without
// re-calling bcp_bind (fast path for multi-batch streaming).

#include "bcp_types.h"

#include <array>
#include <arrow/table.h>

namespace pygim::strategy::mssql::bcp {

namespace rebind_detail {

inline void update_common(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    b.array        = a;
    b.has_nulls    = a->null_count() > 0;
    b.null_bitmap  = a->null_bitmap_data();
    b.array_offset = a->offset();
}

template <typename ArrowArrayT>
inline void update_numeric(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    update_common(b, a);
    auto typed = std::static_pointer_cast<ArrowArrayT>(a);
    b.data_ptr = typed->raw_values();
}

inline void update_bool(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    update_common(b, a);
    auto typed = std::static_pointer_cast<arrow::BooleanArray>(a);
    const auto n = typed->length();
    if (!b.bool_buffer) b.bool_buffer = std::make_shared<std::vector<uint8_t>>();
    b.bool_buffer->resize(static_cast<size_t>(n));
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*b.bool_buffer)[static_cast<size_t>(i)] = typed->Value(i) ? 1 : 0;
    }
    b.data_ptr = b.bool_buffer->data();
}

inline void update_date32(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    update_common(b, a);
    auto typed = std::static_pointer_cast<arrow::Date32Array>(a);
    const auto n = typed->length();
    if (!b.date_buffer) b.date_buffer = std::make_shared<std::vector<SQL_DATE_STRUCT>>();
    b.date_buffer->resize(static_cast<size_t>(n));
    const auto* raw = typed->raw_values();
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*b.date_buffer)[static_cast<size_t>(i)] = days_to_sql_date(raw[i]);
    }
    b.data_ptr = b.date_buffer->data();
}

inline void update_timestamp(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    update_common(b, a);
    auto typed = std::static_pointer_cast<arrow::TimestampArray>(a);
    const auto n = typed->length();
    if (!b.timestamp_buffer) b.timestamp_buffer = std::make_shared<std::vector<SQL_TIMESTAMP_STRUCT>>();
    b.timestamp_buffer->resize(static_cast<size_t>(n));
    const auto* raw = typed->raw_values();
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*b.timestamp_buffer)[static_cast<size_t>(i)] = micros_to_sql_timestamp(raw[i]);
    }
    b.data_ptr = b.timestamp_buffer->data();
}

inline void update_time64(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    update_common(b, a);
    auto typed = std::static_pointer_cast<arrow::Time64Array>(a);
    const auto n = typed->length();
    if (!b.time_buffer) b.time_buffer = std::make_shared<std::vector<SQL_SS_TIME2_STRUCT>>();
    b.time_buffer->resize(static_cast<size_t>(n));
    const auto* raw = typed->raw_values();
    const bool has_nulls = a->null_count() > 0;
    for (int64_t i = 0; i < n; ++i) {
        if (has_nulls && typed->IsNull(i)) continue;
        (*b.time_buffer)[static_cast<size_t>(i)] = nanos_to_sql_time(raw[i]);
    }
    b.data_ptr = b.time_buffer->data();
}

/// Shared preamble for all string/binary rebind: reset variable-length fields.
inline void reset_string_fields(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    update_common(b, a);
    b.str_buf_bound = false;
    b.last_collen   = -2;
    b.offsets32 = nullptr;
    b.offsets64 = nullptr;
    b.str_data  = nullptr;
    b.string_view_array = nullptr;
    b.binary_view_array = nullptr;
}

inline void update_string_utf8(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    reset_string_fields(b, a);
    auto typed = std::static_pointer_cast<arrow::StringArray>(a);
    b.offsets32 = typed->raw_value_offsets();
    b.str_data  = typed->value_data()->data();
}

inline void update_large_string(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    reset_string_fields(b, a);
    auto typed = std::static_pointer_cast<arrow::LargeStringArray>(a);
    b.offsets64 = typed->raw_value_offsets();
    b.str_data  = typed->value_data()->data();
}

inline void update_large_binary(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    reset_string_fields(b, a);
    auto typed = std::static_pointer_cast<arrow::LargeBinaryArray>(a);
    b.offsets64 = typed->raw_value_offsets();
    b.str_data  = typed->value_data()->data();
}

inline void update_string_view(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    reset_string_fields(b, a);
    auto typed = std::static_pointer_cast<arrow::StringViewArray>(a);
    b.string_view_array = typed.get();
}

inline void update_binary_view(ColumnBinding& b, const std::shared_ptr<arrow::Array>& a) {
    reset_string_fields(b, a);
    auto typed = std::static_pointer_cast<arrow::BinaryViewArray>(a);
    b.binary_view_array = typed.get();
}

} // namespace rebind_detail

// ── Dispatch table ──────────────────────────────────────────────────────────

/// Signature for per-type rebind (update) functions.
using RebindFn = void(*)(ColumnBinding&, const std::shared_ptr<arrow::Array>&);

/// Compile-time dispatch table: Arrow type id → rebind function.
/// nullptr = unreachable (bind_columns already validated the type).
inline constexpr auto rebind_dispatch = []() consteval {
    std::array<RebindFn, arrow::Type::MAX_ID> t{};
    t[arrow::Type::INT8]         = &rebind_detail::update_numeric<arrow::Int8Array>;
    t[arrow::Type::INT16]        = &rebind_detail::update_numeric<arrow::Int16Array>;
    t[arrow::Type::INT32]        = &rebind_detail::update_numeric<arrow::Int32Array>;
    t[arrow::Type::INT64]        = &rebind_detail::update_numeric<arrow::Int64Array>;
    t[arrow::Type::UINT8]        = &rebind_detail::update_numeric<arrow::UInt8Array>;
    t[arrow::Type::UINT16]       = &rebind_detail::update_numeric<arrow::UInt16Array>;
    t[arrow::Type::UINT32]       = &rebind_detail::update_numeric<arrow::UInt32Array>;
    t[arrow::Type::UINT64]       = &rebind_detail::update_numeric<arrow::UInt64Array>;
    t[arrow::Type::BOOL]         = &rebind_detail::update_bool;
    t[arrow::Type::FLOAT]        = &rebind_detail::update_numeric<arrow::FloatArray>;
    t[arrow::Type::DOUBLE]       = &rebind_detail::update_numeric<arrow::DoubleArray>;
    t[arrow::Type::DATE32]       = &rebind_detail::update_date32;
    t[arrow::Type::TIMESTAMP]    = &rebind_detail::update_timestamp;
    t[arrow::Type::TIME64]       = &rebind_detail::update_time64;
    t[arrow::Type::DURATION]     = &rebind_detail::update_numeric<arrow::DurationArray>;
    t[arrow::Type::STRING]       = &rebind_detail::update_string_utf8;
    t[arrow::Type::LARGE_STRING] = &rebind_detail::update_large_string;
    t[arrow::Type::LARGE_BINARY] = &rebind_detail::update_large_binary;
    t[arrow::Type::STRING_VIEW]  = &rebind_detail::update_string_view;
    t[arrow::Type::BINARY_VIEW]  = &rebind_detail::update_binary_view;
    return t;
}();

} // namespace pygim::strategy::mssql::bcp
