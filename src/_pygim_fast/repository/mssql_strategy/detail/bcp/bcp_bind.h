#pragma once
// BCP column binding: per-type bind helpers, column classification, staging buffer setup.

#include "bcp_types.h"
#include "../../mssql_strategy.h"

#include <arrow/table.h>

namespace pygim::bcp {

// ── Generic fixed-width binding ─────────────────────────────────────────────

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

    auto ret = bcp.bind(dbc, reinterpret_cast<LPCBYTE>(const_cast<void*>(data)),
                        0, static_cast<DBINT>(stride), nullptr, 0, bcp_type, ordinal);
    if (ret != kSucceed)
        MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_bind");
    return b;
}

// ── Generic string binding ──────────────────────────────────────────────────

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

    auto ret = bcp.bind(dbc, reinterpret_cast<LPCBYTE>(const_cast<uint8_t*>(&dummy)),
                        0, sql_type::varlen_data,
                        reinterpret_cast<LPCBYTE>(const_cast<uint8_t*>(&term)),
                        1, sql_type::character, ordinal);
    if (ret != kSucceed)
        MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_bind");
    return b;
}

// ── Per-type binding functions ──────────────────────────────────────────────

inline ColumnBinding bind_int64(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::Int64Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(int64_t), sql_type::bigint, a, ord);
}

inline ColumnBinding bind_int32(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::Int32Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(int32_t), sql_type::int4, a, ord);
}

inline ColumnBinding bind_uint8(const BcpApi& bcp, SQLHDBC dbc,
                                const std::shared_ptr<arrow::Array>& a, int ord) {
    auto t = std::static_pointer_cast<arrow::UInt8Array>(a);
    return make_fixed_binding(bcp, dbc, t->raw_values(), sizeof(uint8_t), sql_type::bit, a, ord);
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

#if PYGIM_HAVE_ARROW_STRING_VIEW
inline ColumnBinding bind_string_view(const BcpApi& bcp, SQLHDBC dbc,
                                      const std::shared_ptr<arrow::Array>& a, int ord) {
    auto typed = std::static_pointer_cast<arrow::StringViewArray>(a);
    auto b = make_string_binding(bcp, dbc, a, ord);
    b.string_view_array = typed.get();
    return b;
}
#endif

// ── Column dispatcher ───────────────────────────────────────────────────────
/// Bind all columns from a record batch. Returns a vector of ColumnBindings.
inline std::vector<ColumnBinding> bind_columns(
    const BcpApi& bcp, SQLHDBC dbc,
    const std::shared_ptr<arrow::RecordBatch>& batch)
{
    const int n = batch->num_columns();
    std::vector<ColumnBinding> bindings;
    bindings.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        auto field = batch->schema()->field(i);
        auto array = batch->column(i);
        const int ordinal = i + 1;

        switch (field->type()->id()) {
        case arrow::Type::INT64:      bindings.push_back(bind_int64(bcp, dbc, array, ordinal));        break;
        case arrow::Type::INT32:      bindings.push_back(bind_int32(bcp, dbc, array, ordinal));        break;
        case arrow::Type::UINT8:      bindings.push_back(bind_uint8(bcp, dbc, array, ordinal));        break;
        case arrow::Type::DOUBLE:     bindings.push_back(bind_double(bcp, dbc, array, ordinal));       break;
        case arrow::Type::DATE32:     bindings.push_back(bind_date32(bcp, dbc, array, ordinal));       break;
        case arrow::Type::TIMESTAMP:  bindings.push_back(bind_timestamp(bcp, dbc, array, ordinal));    break;
        case arrow::Type::STRING:     bindings.push_back(bind_string(bcp, dbc, array, ordinal));       break;
        case arrow::Type::LARGE_STRING: bindings.push_back(bind_large_string(bcp, dbc, array, ordinal)); break;
#if PYGIM_HAVE_ARROW_STRING_VIEW
        case arrow::Type::STRING_VIEW: bindings.push_back(bind_string_view(bcp, dbc, array, ordinal)); break;
#endif
        default:
            throw std::runtime_error("Unsupported Arrow type: " + field->type()->ToString());
        }
    }
    return bindings;
}

// ── Column classification ───────────────────────────────────────────────────
/// Split bindings into fixed-width and string columns; detect null presence.
inline ClassifiedColumns classify_columns(std::vector<ColumnBinding>& bindings) {
    ClassifiedColumns c;
    c.fixed.reserve(bindings.size());
    c.string.reserve(bindings.size());
    for (auto& b : bindings) {
        (b.is_string ? c.string : c.fixed).push_back(&b);
        if (b.has_nulls) c.any_has_nulls = true;
    }
    return c;
}

// ── Staging buffer ──────────────────────────────────────────────────────────
/// Create a contiguous staging buffer for all fixed-width columns.
/// Each column gets a slot; one-time bcp_colptr + bcp_collen redirect.
inline std::vector<uint8_t> setup_staging(
    const BcpApi& bcp, SQLHDBC dbc,
    std::span<ColumnBinding*> fixed_cols)
{
    size_t total = 0;
    for (auto* bp : fixed_cols) {
        bp->staging_offset = total;
        total += bp->value_stride;
    }
    std::vector<uint8_t> buf(total);
    for (auto* bp : fixed_cols) {
        bcp.colptr(dbc, buf.data() + bp->staging_offset, bp->ordinal);
        bcp.collen(dbc, static_cast<DBINT>(bp->value_stride), bp->ordinal);
    }
    return buf;
}

} // namespace pygim::bcp
