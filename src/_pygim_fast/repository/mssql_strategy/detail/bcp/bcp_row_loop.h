#pragma once
// BCP row loop: dispatch entry point and batch processing.
//
// The actual row-loop logic is delegated to TransposeStrategy implementations
// (see bcp_transpose_strategy.h).  Shared helper functions live in
// bcp_row_helpers.h.  This file provides:
//   - run_row_loop()        — runtime dispatch via BcpContext::transpose
//   - process_batch()       — per-RecordBatch orchestrator (runtime dispatch)
//   - process_batch<T>()    — templated overload; T must be a concrete
//                             TransposeStrategy subtype.  Enables devirtualization
//                             of T::run() when T is a final class.

#include "bcp_transpose_strategy.h"

namespace pygim::bcp {

// ── Row loop dispatcher ─────────────────────────────────────────────────────

/// Default strategy instance (allocated once, never destroyed — intentional).
inline RowMajorTranspose& default_transpose() {
    static RowMajorTranspose instance;
    return instance;
}

inline void run_row_loop(
    BcpContext& ctx,
    std::span<ColumnBinding*> fixed_cols,
    std::span<ColumnBinding*> string_cols,
    std::span<uint8_t> staging,
    int64_t num_rows,
    bool any_has_nulls)
{
    auto& strategy = ctx.transpose ? *ctx.transpose : default_transpose();
    strategy.run(ctx, fixed_cols, string_cols, staging, num_rows, any_has_nulls);
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

// ── Templated overload: concrete Transpose type known at compile time ───────
//
// When Transpose is a final class (RowMajorTranspose, ColumnMajorTranspose),
// the compiler can devirtualize and inline strategy.run() — no vtable lookup
// in the hot row loop.  Called from MssqlStrategy<Transpose>::persist().

template <typename Transpose>
inline void process_batch(BcpContext& ctx,
                          const std::shared_ptr<arrow::RecordBatch>& batch,
                          Transpose& strategy) {
    if (!batch || batch->num_rows() == 0 || batch->num_columns() == 0) return;

    ++ctx.record_batches;
    ctx.processed_rows += batch->num_rows();

    ctx.timer.start_sub_timer("bind_columns", false);
    auto bindings = bind_columns(ctx.bcp, ctx.dbc, batch);
    ctx.timer.stop_sub_timer("bind_columns", false);

    auto [fixed, string, any_nulls] = classify_columns(bindings);
    auto staging = setup_staging(ctx.bcp, ctx.dbc, fixed);

    ctx.timer.start_sub_timer("row_loop", false);
    strategy.run(ctx, fixed, string, staging, batch->num_rows(), any_nulls);
    ctx.timer.stop_sub_timer("row_loop", false);
}

} // namespace pygim::bcp
