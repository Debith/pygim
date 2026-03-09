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
#include "bcp_simd.h"
#include "bcp_simd_kernels.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

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
        const auto send_t0 = hot_timer_start(ctx);
        if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
            odbc::raise_if_error(
                SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
            throw std::runtime_error(
                "bcp_sendrow failed at row " + std::to_string(row));
        }
        hot_timer_add(ctx, send_t0, ctx.sendrow_seconds);
        ++ctx.sent_rows;
        if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
            flush_batch(ctx.bcp, ctx.dbc, ctx);
    }

    static void run_fast(BcpContext& ctx,
                         std::span<ColumnBinding*> fixed,
                         std::span<ColumnBinding*> string,
                         std::span<uint8_t> staging,
                         int64_t num_rows)
    {
        for (int64_t row = 0; row < num_rows; ++row) {
            const auto copy_t0 = hot_timer_start(ctx);
            for (auto* bp : fixed) {
                const auto* src = static_cast<const uint8_t*>(bp->data_ptr)
                                + static_cast<size_t>(row) * bp->value_stride;
                std::memcpy(staging.data() + bp->staging_offset,
                            src, bp->value_stride);
            }
            hot_timer_add(ctx, copy_t0, ctx.fixed_copy_seconds);

            const auto str_t0 = hot_timer_start(ctx);
            for (auto* bp : string)
                handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
            hot_timer_add(ctx, str_t0, ctx.string_pack_seconds);

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
            const auto copy_t0 = hot_timer_start(ctx);
            for (auto* bp : fixed)
                copy_fixed_or_null(ctx.bcp, ctx.dbc, *bp, staging, row);
            hot_timer_add(ctx, copy_t0, ctx.fixed_copy_seconds);

            const auto str_t0 = hot_timer_start(ctx);
            for (auto* bp : string) {
                if (bp->has_nulls && is_null_at(*bp, row)) [[unlikely]] {
                    ctx.bcp.collen(ctx.dbc, SQL_NULL_DATA, bp->ordinal);
                    continue;
                }
                handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
            }
            hot_timer_add(ctx, str_t0, ctx.string_pack_seconds);

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
        // AVX2 is strictly opt-in: only enabled if PYGIM_FORCE_SIMD=avx2
        bool force_avx2 = false;
        if (const char* forced = std::getenv("PYGIM_FORCE_SIMD")) {
            std::string mode(forced);
            std::transform(mode.begin(), mode.end(), mode.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (mode == "avx2")
                force_avx2 = true;
        }

        // Profile-aware activation: only enable AVX2 if block count >= 2
        constexpr int kMinAvx2Blocks = 2;
        auto avx2_blocks = simd::plan_avx2_blocks(fixed);
        bool eligible_avx2 = force_avx2 && (avx2_blocks.size() >= kMinAvx2Blocks);

        if (eligible_avx2) {
            ctx.simd_level = "avx2";
            run_impl<Simd::Avx2>(ctx, fixed, string, staging, num_rows, any_nulls);
        } else {
            ctx.simd_level = "scalar";
            run_impl<Simd::Scalar>(ctx, fixed, string, staging, num_rows, any_nulls);
        }
    }

private:
    int64_t m_mini_batch;

    template <Simd S>
    void run_impl(BcpContext& ctx,
                  std::span<ColumnBinding*> fixed,
                  std::span<ColumnBinding*> string,
                  std::span<uint8_t> staging,
                  int64_t num_rows,
                  bool any_nulls)
    {
        if constexpr (S == Simd::Avx2) {
            if (!any_nulls)
                run_fast_avx2(ctx, fixed, string, staging, num_rows);
            else
                run_general(ctx, fixed, string, staging, num_rows);
        } else if constexpr (S == Simd::Scalar) {
            if (!any_nulls)
                run_fast_scalar(ctx, fixed, string, staging, num_rows);
            else
                run_general(ctx, fixed, string, staging, num_rows);
        } else {
            static_assert(S == Simd::Scalar || S == Simd::Avx2,
                          "Unhandled SIMD level in ColumnMajorTranspose::run_impl");
        }
    }

    // Compute the row width (sum of all fixed-column strides).
    static size_t row_width(std::span<ColumnBinding*> fixed) {
        size_t w = 0;
        for (auto* bp : fixed) w += bp->value_stride;
        return w;
    }

    void run_fast_avx2(BcpContext& ctx,
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

        std::vector<uint8_t> copied(fixed.size(), 0);
        std::vector<simd::Avx2BlockPlan> avx2_blocks;
        std::vector<simd::Avx2BlockPlan64> avx2_blocks64;
        const bool enable_avx2_8b = [] {
            const char* raw = std::getenv("PYGIM_ENABLE_AVX2_8B");
            return raw && *raw != '\0' && std::string(raw) != "0";
        }();
        if constexpr (simd::kCanBuildAvx2Kernel) {
            avx2_blocks = simd::plan_avx2_blocks(fixed);
            if (enable_avx2_8b) {
                avx2_blocks64 = simd::plan_avx2_blocks64(fixed);
            }
        }

        for (int64_t base = 0; base < num_rows; base += mb) {
            const int64_t end = std::min(base + mb, num_rows);
            const int64_t chunk = end - base;

            // ── Column-major fill: sequential reads from Arrow buffers ──
            if (!multi_staging.empty()) {
                const auto copy_t0 = hot_timer_start(ctx);
                std::fill(copied.begin(), copied.end(), static_cast<uint8_t>(0));

                if constexpr (simd::kCanBuildAvx2Kernel) {
                    // AVX2 path: preplanned contiguous 8x4-byte column blocks.
                    for (const auto& block : avx2_blocks) {
                        const std::size_t c = block.start_index;
                        std::array<const int32_t*, 8> src_cols{};
                        for (std::size_t j = 0; j < 8; ++j) {
                            src_cols[j] = static_cast<const int32_t*>(fixed[c + j]->data_ptr)
                                        + base;
                            copied[c + j] = 1;
                        }

                        int64_t r = 0;
                        for (; r + 8 <= chunk; r += 8) {
                            simd::transpose8x8_i32(
                                src_cols,
                                r,
                                multi_staging.data(),
                                rw,
                                block.dst_col_offset);
                        }
                        // Tail rows for this 8-column block.
                        for (; r < chunk; ++r) {
                            uint8_t* row_ptr = multi_staging.data() + static_cast<size_t>(r) * rw;
                            for (std::size_t j = 0; j < 8; ++j) {
                                std::memcpy(row_ptr + block.dst_col_offset + j * 4,
                                            src_cols[j] + r,
                                            4);
                            }
                        }
                    }

                    // AVX2 path: preplanned contiguous 4x8-byte column blocks.
                    if (enable_avx2_8b) {
                        for (const auto& block : avx2_blocks64) {
                            const std::size_t c = block.start_index;
                            std::array<const int64_t*, 4> src_cols{};
                            for (std::size_t j = 0; j < 4; ++j) {
                                src_cols[j] = static_cast<const int64_t*>(fixed[c + j]->data_ptr)
                                            + base;
                                copied[c + j] = 1;
                            }

                            int64_t r = 0;
                            for (; r + 4 <= chunk; r += 4) {
                                simd::transpose4x4_i64(
                                    src_cols,
                                    r,
                                    multi_staging.data(),
                                    rw,
                                    block.dst_col_offset);
                            }
                            for (; r < chunk; ++r) {
                                uint8_t* row_ptr = multi_staging.data() + static_cast<size_t>(r) * rw;
                                for (std::size_t j = 0; j < 4; ++j) {
                                    std::memcpy(row_ptr + block.dst_col_offset + j * 8,
                                                src_cols[j] + r,
                                                8);
                                }
                            }
                        }
                    }
                }

                // Scalar fallback for remaining or non-eligible columns.
                for (std::size_t i = 0; i < fixed.size(); ++i) {
                    if (copied[i]) continue;
                    auto* bp = fixed[i];
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
                hot_timer_add(ctx, copy_t0, ctx.fixed_copy_seconds);
            }

            // ── Sendrow: copy pre-filled row into staging, then sendrow ──
            //
            // Each row from multi_staging is memcpy'd into the original staging
            // buffer (where BCP pointers already point from setup_staging).
            // Zero bcp_colptr calls needed — one memcpy per row replaces
            // N colptr redirects.
            for (int64_t r = 0; r < chunk; ++r) {
                const int64_t abs_row = base + r;

                if (!multi_staging.empty()) {
                    const auto redirect_t0 = hot_timer_start(ctx);
                    std::memcpy(staging.data(),
                                multi_staging.data() + static_cast<size_t>(r) * rw,
                                rw);
                    hot_timer_add(ctx, redirect_t0, ctx.colptr_redirect_seconds);
                }

                const auto str_t0 = hot_timer_start(ctx);
                for (auto* bp : string)
                    handle_string_column(ctx.bcp, ctx.dbc, *bp, abs_row);
                hot_timer_add(ctx, str_t0, ctx.string_pack_seconds);

                const auto send_t0 = hot_timer_start(ctx);
                if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
                    odbc::raise_if_error(
                        SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
                    throw std::runtime_error(
                        "bcp_sendrow failed at row " + std::to_string(abs_row));
                }
                hot_timer_add(ctx, send_t0, ctx.sendrow_seconds);
                ++ctx.sent_rows;
                if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
                    flush_batch(ctx.bcp, ctx.dbc, ctx);
            }
        }
    }

    void run_fast_scalar(BcpContext& ctx,
                         std::span<ColumnBinding*> fixed,
                         std::span<ColumnBinding*> string,
                         std::span<uint8_t> staging,
                         int64_t num_rows)
    {
        const size_t rw = row_width(fixed);
        const int64_t mb = m_mini_batch;
        std::vector<uint8_t> multi_staging;
        if (rw > 0 && fixed.size() > 0)
            multi_staging.resize(static_cast<size_t>(mb) * rw);

        for (int64_t base = 0; base < num_rows; base += mb) {
            const int64_t end = std::min(base + mb, num_rows);
            const int64_t chunk = end - base;

            if (!multi_staging.empty()) {
                const auto copy_t0 = hot_timer_start(ctx);
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
                hot_timer_add(ctx, copy_t0, ctx.fixed_copy_seconds);
            }

            for (int64_t r = 0; r < chunk; ++r) {
                const int64_t abs_row = base + r;

                if (!multi_staging.empty()) {
                    const auto redirect_t0 = hot_timer_start(ctx);
                    std::memcpy(staging.data(),
                                multi_staging.data() + static_cast<size_t>(r) * rw,
                                rw);
                    hot_timer_add(ctx, redirect_t0, ctx.colptr_redirect_seconds);
                }

                const auto str_t0 = hot_timer_start(ctx);
                for (auto* bp : string)
                    handle_string_column(ctx.bcp, ctx.dbc, *bp, abs_row);
                hot_timer_add(ctx, str_t0, ctx.string_pack_seconds);

                const auto send_t0 = hot_timer_start(ctx);
                if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
                    odbc::raise_if_error(
                        SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
                    throw std::runtime_error(
                        "bcp_sendrow failed at row " + std::to_string(abs_row));
                }
                hot_timer_add(ctx, send_t0, ctx.sendrow_seconds);
                ++ctx.sent_rows;
                if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
                    flush_batch(ctx.bcp, ctx.dbc, ctx);
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
                const auto copy_t0 = hot_timer_start(ctx);
                for (auto* bp : fixed)
                    copy_fixed_or_null(ctx.bcp, ctx.dbc, *bp, staging, row);
                hot_timer_add(ctx, copy_t0, ctx.fixed_copy_seconds);

                const auto str_t0 = hot_timer_start(ctx);
                for (auto* bp : string) {
                    if (bp->has_nulls && is_null_at(*bp, row)) [[unlikely]] {
                        ctx.bcp.collen(ctx.dbc, SQL_NULL_DATA, bp->ordinal);
                        continue;
                    }
                    handle_string_column(ctx.bcp, ctx.dbc, *bp, row);
                }
                hot_timer_add(ctx, str_t0, ctx.string_pack_seconds);

                const auto send_t0 = hot_timer_start(ctx);
                if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]] {
                    odbc::raise_if_error(
                        SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
                    throw std::runtime_error(
                        "bcp_sendrow failed at row " + std::to_string(row));
                }
                hot_timer_add(ctx, send_t0, ctx.sendrow_seconds);
                ++ctx.sent_rows;
                if (ctx.sent_rows % ctx.batch_size == 0) [[unlikely]]
                    flush_batch(ctx.bcp, ctx.dbc, ctx);
            }
        }
    }
};

} // namespace pygim::bcp
