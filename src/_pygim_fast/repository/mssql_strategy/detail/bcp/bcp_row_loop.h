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
//
// Both process_batch overloads accept a BatchBindingState& for cross-batch
// binding reuse.  When the schema is unchanged, bcp_bind and bcp_colptr
// calls are skipped — only Arrow data pointers are updated.

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

// ── Binding setup / update ──────────────────────────────────────────────────

/// Full bind: first batch, or schema changed.  Calls bcp_bind + bcp_colptr.
inline void full_bind(BcpContext& ctx,
                      BatchBindingState& state,
                      const std::shared_ptr<arrow::RecordBatch>& batch) {
    state.bindings = bind_columns(ctx.bcp, ctx.dbc, batch);
    state.classified = classify_columns(state.bindings);
    state.staging = setup_staging(ctx.bcp, ctx.dbc, state.classified.fixed);
    state.schema = batch->schema();
    state.initialized = true;
}

/// Fast rebind: schema unchanged.  Skips bcp_bind/colptr, updates Arrow pointers.
inline void fast_rebind(BcpContext& ctx,
                        BatchBindingState& state,
                        const std::shared_ptr<arrow::RecordBatch>& batch) {
    (void)ctx;  // staging buffer address is stable — no bcp_colptr needed
    rebind_columns(state.bindings, state.classified, batch);
}

// ── Process one Arrow RecordBatch ───────────────────────────────────────────

inline void process_batch(BcpContext& ctx,
                          const std::shared_ptr<arrow::RecordBatch>& batch,
                          BatchBindingState& state) {
    if (!batch || batch->num_rows() == 0 || batch->num_columns() == 0) return;

    ++ctx.record_batches;
    ctx.processed_rows += batch->num_rows();

    stage_timer_start(ctx, ctx.timer_bind_columns_id, "bind_columns");
    if (state.matches(batch->schema()))
        fast_rebind(ctx, state, batch);
    else
        full_bind(ctx, state, batch);
    stage_timer_stop(ctx, ctx.timer_bind_columns_id, "bind_columns");

    stage_timer_start(ctx, ctx.timer_row_loop_id, "row_loop");
    run_row_loop(ctx, state.classified.fixed, state.classified.string,
                 state.staging, batch->num_rows(), state.classified.any_has_nulls);
    stage_timer_stop(ctx, ctx.timer_row_loop_id, "row_loop");
}

// ── Templated overload: concrete Transpose type known at compile time ───────

template <typename Transpose>
inline void process_batch(BcpContext& ctx,
                          const std::shared_ptr<arrow::RecordBatch>& batch,
                          Transpose& strategy,
                          BatchBindingState& state) {
    if (!batch || batch->num_rows() == 0 || batch->num_columns() == 0) return;

    ++ctx.record_batches;
    ctx.processed_rows += batch->num_rows();

    stage_timer_start(ctx, ctx.timer_bind_columns_id, "bind_columns");
    if (state.matches(batch->schema()))
        fast_rebind(ctx, state, batch);
    else
        full_bind(ctx, state, batch);
    stage_timer_stop(ctx, ctx.timer_bind_columns_id, "bind_columns");

    stage_timer_start(ctx, ctx.timer_row_loop_id, "row_loop");
    strategy.run(ctx, state.classified.fixed, state.classified.string,
                 state.staging, batch->num_rows(), state.classified.any_has_nulls);
    stage_timer_stop(ctx, ctx.timer_row_loop_id, "row_loop");
}

} // namespace pygim::bcp
