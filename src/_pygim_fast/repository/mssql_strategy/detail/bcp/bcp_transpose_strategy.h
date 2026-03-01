#pragma once
// BCP Transpose Strategy: pluggable approaches for the columnar→row transpose
// in the BCP row loop.  Each strategy controls how fixed-width column data is
// gathered from Arrow's columnar buffers into the staging row buffer before
// bcp_sendrow.
//
// Strategy hierarchy:
//   TransposeStrategy (abstract)
//     ├── RowMajorTranspose    — current approach: per-row, per-column memcpy
//     └── ColumnMajorTranspose — mini-batch: column-major fill, then sendrow
//
// Adding a new strategy (e.g., SIMD):
//   1. Subclass TransposeStrategy, implement run().
//   2. Optionally override name() for metrics/logging.
//   3. Set it on BcpContext::transpose before calling process_batch.

#include "bcp_row_helpers.h"

namespace pygim::bcp {

// ── Abstract base ───────────────────────────────────────────────────────────

/// Interface for row-loop transpose strategies.  Each implementation controls
/// how the inner loop (fixed copy + string handling + sendrow + batch flush)
/// is structured.
struct TransposeStrategy {
    virtual ~TransposeStrategy() = default;

    /// Execute the row loop for one Arrow RecordBatch worth of rows.
    /// @param ctx       BCP session state (bcp api, dbc, batch_size, counters).
    /// @param fixed     Fixed-width columns (already bcp_bind'd, staging set up).
    /// @param string    String columns (already bcp_bind'd).
    /// @param staging   Contiguous staging buffer; colptr already points here.
    /// @param num_rows  Number of rows in this RecordBatch.
    /// @param any_nulls True if any column has nulls → must check bitmaps.
    virtual void run(BcpContext& ctx,
                     std::span<ColumnBinding*> fixed,
                     std::span<ColumnBinding*> string,
                     std::span<uint8_t> staging,
                     int64_t num_rows,
                     bool any_nulls) = 0;

    /// Human-readable name for logging / metrics.
    [[nodiscard]] virtual const char* name() const noexcept = 0;
};

// ── Row-major (current default) ─────────────────────────────────────────────

/// Original approach: iterate rows, then columns within each row.
/// Simple and correct.  Read pattern jumps between column buffers each row,
/// which can cause cache thrashing on wide tables.
struct RowMajorTranspose final : TransposeStrategy {
    [[nodiscard]] const char* name() const noexcept override { return "row_major"; }

    void run(BcpContext& ctx,
             std::span<ColumnBinding*> fixed,
             std::span<ColumnBinding*> string,
             std::span<uint8_t> staging,
             int64_t num_rows,
             bool any_nulls) override
    {
        if (!any_nulls)
            run_fast(ctx, fixed, string, staging, num_rows);
        else
            run_general(ctx, fixed, string, staging, num_rows);
    }

private:
    static void send_and_flush(BcpContext& ctx, int64_t row) {
        if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
            MssqlStrategyNative::raise_if_error(
                SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
            throw std::runtime_error(
                "bcp_sendrow failed at row " + std::to_string(row));
        }
        ++ctx.sent_rows;
        if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
            flush_batch(ctx.bcp, ctx.dbc, ctx.timer);
    }

    static void run_fast(BcpContext& ctx,
                         std::span<ColumnBinding*> fixed,
                         std::span<ColumnBinding*> string,
                         std::span<uint8_t> staging,
                         int64_t num_rows)
    {
        for (int64_t row = 0; row < num_rows; ++row) {
            for (auto* bp : fixed) {
                const auto* src = static_cast<const uint8_t*>(bp->data_ptr)
                                + static_cast<size_t>(row) * bp->value_stride;
                std::memcpy(staging.data() + bp->staging_offset,
                            src, bp->value_stride);
            }
            for (auto* bp : string)
                handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
            send_and_flush(ctx, row);
        }
    }

    static void run_general(BcpContext& ctx,
                            std::span<ColumnBinding*> fixed,
                            std::span<ColumnBinding*> string,
                            std::span<uint8_t> staging,
                            int64_t num_rows)
    {
        for (int64_t row = 0; row < num_rows; ++row) {
            for (auto* bp : fixed)
                copy_fixed_or_null(ctx.bcp, ctx.dbc, *bp, staging, row);
            for (auto* bp : string) {
                if (bp->has_nulls && is_null_at(*bp, row)) [[unlikely]] {
                    ctx.bcp.collen(ctx.dbc, SQL_NULL_DATA, bp->ordinal);
                    continue;
                }
                handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
            }
            send_and_flush(ctx, row);
        }
    }
};

// ── Column-major mini-batch ─────────────────────────────────────────────────

/// Cache-friendly approach: transpose a mini-batch of rows column-by-column
/// (sequential reads from Arrow buffers), then sendrow each pre-filled row.
/// Layout enables future SIMD vectorization of the inner copy loop.
///
/// Mini-batch size is tuned to fit in L1 cache (~32–48 KB).
/// For a 128-byte row width, 128 rows × 128 B = 16 KB — well within L1.
struct ColumnMajorTranspose final : TransposeStrategy {
    static constexpr int64_t kDefaultMiniBatch = 128;

    explicit ColumnMajorTranspose(int64_t mini_batch = kDefaultMiniBatch)
        : m_mini_batch(mini_batch) {}

    [[nodiscard]] const char* name() const noexcept override { return "column_major"; }

    void run(BcpContext& ctx,
             std::span<ColumnBinding*> fixed,
             std::span<ColumnBinding*> string,
             std::span<uint8_t> staging,
             int64_t num_rows,
             bool any_nulls) override
    {
        if (!any_nulls)
            run_fast(ctx, fixed, string, staging, num_rows);
        else
            run_general(ctx, fixed, string, staging, num_rows);
    }

private:
    int64_t m_mini_batch;

    // Compute the row width (sum of all fixed-column strides).
    static size_t row_width(std::span<ColumnBinding*> fixed) {
        size_t w = 0;
        for (auto* bp : fixed) w += bp->value_stride;
        return w;
    }

    void run_fast(BcpContext& ctx,
                  std::span<ColumnBinding*> fixed,
                  std::span<ColumnBinding*> string,
                  std::span<uint8_t> staging,
                  int64_t num_rows)
    {
        const size_t rw = row_width(fixed);
        // Multi-row staging buffer: m_mini_batch rows × rw bytes each.
        // Falls back to single-row staging (the original `staging` param)
        // if there are no fixed columns.
        const int64_t mb = m_mini_batch;
        std::vector<uint8_t> multi_staging;
        if (rw > 0 && fixed.size() > 0)
            multi_staging.resize(static_cast<size_t>(mb) * rw);

        for (int64_t base = 0; base < num_rows; base += mb) {
            const int64_t end = std::min(base + mb, num_rows);
            const int64_t chunk = end - base;

            // ── Column-major fill: sequential reads from Arrow buffers ──
            if (!multi_staging.empty()) {
                for (auto* bp : fixed) {
                    const auto* col_src =
                        static_cast<const uint8_t*>(bp->data_ptr)
                        + static_cast<size_t>(base) * bp->value_stride;
                    for (int64_t r = 0; r < chunk; ++r) {
                        std::memcpy(
                            multi_staging.data()
                                + static_cast<size_t>(r) * rw
                                + bp->staging_offset,
                            col_src + static_cast<size_t>(r) * bp->value_stride,
                            bp->value_stride);
                    }
                }
            }

            // ── Row-major sendrow from pre-filled buffer ──
            for (int64_t r = 0; r < chunk; ++r) {
                const int64_t abs_row = base + r;

                // Point BCP at this row's data in the multi-row buffer.
                if (!multi_staging.empty()) {
                    const auto* row_ptr =
                        multi_staging.data() + static_cast<size_t>(r) * rw;
                    for (auto* bp : fixed) {
                        std::memcpy(staging.data() + bp->staging_offset,
                                    row_ptr + bp->staging_offset,
                                    bp->value_stride);
                    }
                }

                for (auto* bp : string)
                    handle_string_column(ctx.bcp, ctx.dbc, *bp, abs_row);

                if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
                    MssqlStrategyNative::raise_if_error(
                        SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
                    throw std::runtime_error(
                        "bcp_sendrow failed at row " + std::to_string(abs_row));
                }
                ++ctx.sent_rows;
                if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
                    flush_batch(ctx.bcp, ctx.dbc, ctx.timer);
            }
        }
    }

    void run_general(BcpContext& ctx,
                     std::span<ColumnBinding*> fixed,
                     std::span<ColumnBinding*> string,
                     std::span<uint8_t> staging,
                     int64_t num_rows)
    {
        // For the null-aware path, per-cell null checks break the pure
        // column-major pattern.  We still mini-batch for prefetch locality,
        // but fall back to per-cell null handling within each mini-batch.
        const int64_t mb = m_mini_batch;

        for (int64_t base = 0; base < num_rows; base += mb) {
            const int64_t end = std::min(base + mb, num_rows);

            for (int64_t row = base; row < end; ++row) {
                for (auto* bp : fixed)
                    copy_fixed_or_null(ctx.bcp, ctx.dbc, *bp, staging, row);
                for (auto* bp : string) {
                    if (bp->has_nulls && is_null_at(*bp, row)) [[unlikely]] {
                        ctx.bcp.collen(ctx.dbc, SQL_NULL_DATA, bp->ordinal);
                        continue;
                    }
                    handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
                }
                if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
                    MssqlStrategyNative::raise_if_error(
                        SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
                    throw std::runtime_error(
                        "bcp_sendrow failed at row " + std::to_string(row));
                }
                ++ctx.sent_rows;
                if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
                    flush_batch(ctx.bcp, ctx.dbc, ctx.timer);
            }
        }
    }
};

} // namespace pygim::bcp
