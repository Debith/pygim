#pragma once
// BCP row loop: string column handling, fast/general path iteration, batch flush.
// Replaces the PYGIM_BCP_HANDLE_STRING_COL / PYGIM_BCP_MAYBE_FLUSH_BATCH macros
// with type-safe inline functions.

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
        MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_batch");
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

// ── Fast path — no column has nulls ─────────────────────────────────────────

inline void run_fast_row_loop(
    BcpContext& ctx,
    std::span<ColumnBinding*> fixed_cols,
    std::span<ColumnBinding*> string_cols,
    std::span<uint8_t> staging,
    int64_t num_rows)
{
    for (int64_t row = 0; row < num_rows; ++row) {
        for (auto* bp : fixed_cols) {
            const auto* src = static_cast<const uint8_t*>(bp->data_ptr)
                            + static_cast<size_t>(row) * bp->value_stride;
            std::memcpy(staging.data() + bp->staging_offset, src, bp->value_stride);
        }
        for (auto* bp : string_cols) {
            handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
        }
        if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
            MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
            throw std::runtime_error("bcp_sendrow failed at row " + std::to_string(row));
        }
        ++ctx.sent_rows;
        if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
            flush_batch(ctx.bcp, ctx.dbc, ctx.timer);
    }
}

// ── General path — at least one column has nulls ────────────────────────────

inline void run_general_row_loop(
    BcpContext& ctx,
    std::span<ColumnBinding*> fixed_cols,
    std::span<ColumnBinding*> string_cols,
    std::span<uint8_t> staging,
    int64_t num_rows)
{
    for (int64_t row = 0; row < num_rows; ++row) {
        for (auto* bp : fixed_cols)
            copy_fixed_or_null(ctx.bcp, ctx.dbc, *bp, staging, row);

        for (auto* bp : string_cols) {
            if (bp->has_nulls && is_null_at(*bp, row)) [[unlikely]] {
                ctx.bcp.collen(ctx.dbc, SQL_NULL_DATA, bp->ordinal);
                continue;
            }
            handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
        }
        if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
            MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
            throw std::runtime_error("bcp_sendrow failed at row " + std::to_string(row));
        }
        ++ctx.sent_rows;
        if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
            flush_batch(ctx.bcp, ctx.dbc, ctx.timer);
    }
}

// ── Row loop dispatcher ─────────────────────────────────────────────────────

inline void run_row_loop(
    BcpContext& ctx,
    std::span<ColumnBinding*> fixed_cols,
    std::span<ColumnBinding*> string_cols,
    std::span<uint8_t> staging,
    int64_t num_rows,
    bool any_has_nulls)
{
    if (!any_has_nulls)
        run_fast_row_loop(ctx, fixed_cols, string_cols, staging, num_rows);
    else
        run_general_row_loop(ctx, fixed_cols, string_cols, staging, num_rows);
}

// ── Process one Arrow RecordBatch ───────────────────────────────────────────

inline void process_batch(BcpContext& ctx,
                          const std::shared_ptr<arrow::RecordBatch>& batch) {
    if (!batch || batch->num_rows() == 0 || batch->num_columns() == 0) return;

    ++ctx.record_batches;
    ctx.processed_rows += batch->num_rows();

    ctx.timer.start_sub_timer("bind_columns", false);
    auto bindings = bind_columns(ctx.bcp, ctx.dbc, batch);
    ctx.timer.stop_sub_timer("bind_columns", false);

    auto [fixed, string, any_nulls] = classify_columns(bindings);
    auto staging = setup_staging(ctx.bcp, ctx.dbc, fixed);

    ctx.timer.start_sub_timer("row_loop", false);
    run_row_loop(ctx, fixed, string, staging, batch->num_rows(), any_nulls);
    ctx.timer.stop_sub_timer("row_loop", false);
}

} // namespace pygim::bcp
