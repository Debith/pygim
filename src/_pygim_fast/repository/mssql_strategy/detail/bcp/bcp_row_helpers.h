#pragma once
// BCP row helpers: string column handling, batch flush, fixed-or-null copy.
// These are shared primitives consumed by all TransposeStrategy implementations
// and must be defined before the strategy headers that call them.

#include "bcp_bind.h"
#include "../../../../utils/quick_timer.h"

namespace pygim::bcp {

// ── String column handling ──────────────────────────────────────────────────

/// Copy one string value into the column's reusable buffer, null-terminate,
/// set bcp_collen, and redirect bcp_colptr only when the buffer reallocates.
inline void handle_string_column(const BcpApi& bcp, SQLHDBC dbc,
                                 ColumnBinding& b, int64_t row) {
    DBINT len{};
    const uint8_t* ptr{};

    if (b.offsets32) {
        const auto start = b.offsets32[row];
        len = static_cast<DBINT>(b.offsets32[row + 1] - start);
        ptr = b.str_data + start;
    } else if (b.offsets64) {
        const auto start = b.offsets64[row];
        len = static_cast<DBINT>(b.offsets64[row + 1] - start);
        ptr = b.str_data + start;
    }
#if PYGIM_HAVE_ARROW_STRING_VIEW
    else if (b.string_view_array) {
        auto view = b.string_view_array->GetView(row);
        len = static_cast<DBINT>(view.size());
        ptr = reinterpret_cast<const uint8_t*>(view.data());
    }
#endif
    else { return; /* unreachable for valid bindings */ }

    const auto ulen = static_cast<size_t>(len);
    if (ulen + 1 > b.str_buf.size()) {
        b.str_buf.resize(ulen + 1);
        b.str_buf_bound = false;
    }
    std::memcpy(b.str_buf.data(), ptr, ulen);
    b.str_buf[ulen] = '\0';
    bcp.collen(dbc, len, b.ordinal);

    if (!b.str_buf_bound) [[unlikely]] {
        bcp.colptr(dbc, b.str_buf.data(), b.ordinal);
        b.str_buf_bound = true;
    }
}

// ── Batch flush ─────────────────────────────────────────────────────────────

inline void flush_batch(const BcpApi& bcp, SQLHDBC dbc, QuickTimer& timer) {
    timer.stop_sub_timer("row_loop", false);
    timer.start_sub_timer("batch_flush", false);
    auto ret = bcp.batch(dbc);
    timer.stop_sub_timer("batch_flush", false);
    if (ret == -1) [[unlikely]]
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_batch");
    timer.start_sub_timer("row_loop", false);
}

// ── Fixed column with null toggle ───────────────────────────────────────────

inline void copy_fixed_or_null(const BcpApi& bcp, SQLHDBC dbc,
                               ColumnBinding& b, std::span<uint8_t> staging,
                               int64_t row) {
    if (b.has_nulls && is_null_at(b, row)) [[unlikely]] {
        bcp.collen(dbc, SQL_NULL_DATA, b.ordinal);
    } else {
        const auto* src = static_cast<const uint8_t*>(b.data_ptr)
                        + static_cast<size_t>(row) * b.value_stride;
        std::memcpy(staging.data() + b.staging_offset, src, b.value_stride);
        if (b.has_nulls) [[unlikely]]
            bcp.collen(dbc, static_cast<DBINT>(b.value_stride), b.ordinal);
    }
}

} // namespace pygim::bcp
