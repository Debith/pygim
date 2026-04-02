#pragma once
// BCP Pipeline: single-connection and parallel bulk insert orchestrator.
// Combines archive's bcp_strategy.cpp, bcp_row_loop.h, bcp_entry.h into a
// single header-only file with simplified timing (stage-level chrono only).

#include "bcp_helpers.h"
#include "bcp_pool.h"
#include "../sql_helpers.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <arrow/table.h>
#include <arrow/table_builder.h>

namespace pygim::strategy::mssql::bcp {

// ── BcpMetrics ──────────────────────────────────────────────────────────────

/// Wall-clock timing metrics for a single BCP bulk insert invocation.
/// All `*_seconds` fields are wall-clock durations (not CPU time).
/// For parallel inserts, `row_loop_seconds` and `batch_flush_seconds` are
/// max-across-workers (wall-clock), while row/batch counts are summed.
struct BcpMetrics {
    double  total_seconds{0};         ///< End-to-end wall time.
    double  connect_seconds{0};       ///< Connection pool establishment time.
    double  bind_seconds{0};          ///< Column binding (accumulated across batches).
    double  row_loop_seconds{0};      ///< Row loop including bind within process_batch.
    double  batch_flush_seconds{0};   ///< Final bcp_batch + bcp_done flush.
    int64_t processed_rows{0};        ///< Rows read from Arrow RecordBatches.
    int64_t sent_rows{0};             ///< Rows successfully sent via bcp_sendrow.
    int64_t record_batches{0};        ///< Number of Arrow RecordBatches processed.

    /// Element-wise accumulation (all fields summed).
    BcpMetrics& operator+=(const BcpMetrics& rhs) noexcept {
        total_seconds       += rhs.total_seconds;
        connect_seconds     += rhs.connect_seconds;
        bind_seconds        += rhs.bind_seconds;
        row_loop_seconds    += rhs.row_loop_seconds;
        batch_flush_seconds += rhs.batch_flush_seconds;
        processed_rows      += rhs.processed_rows;
        sent_rows           += rhs.sent_rows;
        record_batches      += rhs.record_batches;
        return *this;
    }

    /// Element-wise sum of two metrics objects.
    friend BcpMetrics operator+(BcpMetrics lhs, const BcpMetrics& rhs) noexcept {
        return lhs += rhs;
    }

    /// Merge a parallel worker's metrics: max for wall-clock timings, sum for counts.
    /// connect_seconds and total_seconds are left untouched (set by the orchestrator).
    BcpMetrics& merge_parallel(const BcpMetrics& worker) noexcept {
        bind_seconds        += worker.bind_seconds;
        row_loop_seconds     = std::max(row_loop_seconds, worker.row_loop_seconds);
        batch_flush_seconds  = std::max(batch_flush_seconds, worker.batch_flush_seconds);
        processed_rows      += worker.processed_rows;
        sent_rows           += worker.sent_rows;
        record_batches      += worker.record_batches;
        return *this;
    }
};

// ── RAII BcpSessionGuard ────────────────────────────────────────────────────

/// Calls bcp_done() on destruction unless dismiss()'d.
/// Prevents leaving ODBC in undefined BCP state on exception.
class BcpSessionGuard {
public:
    BcpSessionGuard(const BcpApi& api, SQLHDBC dbc) noexcept
        : m_api(api), m_dbc(dbc) {}

    ~BcpSessionGuard() noexcept {
        if (!m_dismissed && m_dbc != SQL_NULL_HDBC)
            m_api.done(m_dbc);
    }

    void dismiss() noexcept { m_dismissed = true; }

    BcpSessionGuard(const BcpSessionGuard&) = delete;
    BcpSessionGuard& operator=(const BcpSessionGuard&) = delete;

private:
    const BcpApi& m_api;
    SQLHDBC       m_dbc;
    bool          m_dismissed{false};
};

// ── init_session ────────────────────────────────────────────────────────────

/// bcp_init + optional TABLOCK hint.
inline void init_session(const BcpApi& api, SQLHDBC dbc,
                         const std::string& qualified_table,
                         const std::string& hint) {
    // Convert ASCII table name to UTF-16 for bcp_initW
    std::u16string w;
    w.reserve(qualified_table.size());
    for (char ch : qualified_table) w.push_back(static_cast<char16_t>(ch));

    if (api.init(dbc, reinterpret_cast<LPCWSTR>(w.c_str()),
                 nullptr, nullptr, DB_IN) != kSucceed) {
        odbc::raise_if_error(
            SQL_ERROR, SQL_HANDLE_DBC, dbc,
            ("bcp_init(table=" + qualified_table + ")").c_str());
    }
    if (!hint.empty() && api.control) {
        if (api.control(dbc, kBcpHints,
                        const_cast<char*>(hint.c_str())) != kSucceed) {
            odbc::raise_if_error(
                SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_control(BCPHINTS)");
        }
    }
}

// ── Row loops ───────────────────────────────────────────────────────────────

/// Fast path: no nulls in any column. Straight memcpy + sendrow.
inline void row_loop_fast(BcpContext& ctx,
                          std::span<ColumnBinding*> fixed,
                          std::span<ColumnBinding*> string,
                          std::span<uint8_t> staging,
                          int64_t num_rows) {
    for (int64_t row = 0; row < num_rows; ++row) {
        for (auto* bp : fixed) {
            const auto* src = static_cast<const uint8_t*>(bp->data_ptr)
                            + static_cast<size_t>(row) * bp->value_stride;
            std::memcpy(staging.data() + bp->staging_offset, src, bp->value_stride);
        }
        for (auto* bp : string)
            handle_string_column(ctx.bcp, ctx.dbc, *bp, row);

        if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]]
            odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
        ++ctx.sent_rows;
        if (--ctx.rows_until_flush <= 0) [[unlikely]]
            flush_batch(ctx.bcp, ctx.dbc, ctx);
    }
}

/// General path: some columns have nulls. Per-row null checks.
inline void row_loop_general(BcpContext& ctx,
                             std::span<ColumnBinding*> fixed,
                             std::span<ColumnBinding*> string,
                             std::span<uint8_t> staging,
                             int64_t num_rows) {
    for (int64_t row = 0; row < num_rows; ++row) {
        for (auto* bp : fixed)
            copy_fixed_or_null(ctx.bcp, ctx.dbc, *bp, staging, row);

        for (auto* bp : string)
            handle_string_column(ctx.bcp, ctx.dbc, *bp, row);

        if (ctx.bcp.sendrow(ctx.dbc) != kSucceed) [[unlikely]]
            odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_sendrow");
        ++ctx.sent_rows;
        if (--ctx.rows_until_flush <= 0) [[unlikely]]
            flush_batch(ctx.bcp, ctx.dbc, ctx);
    }
}

/// Dispatch to fast or general path based on null presence.
inline void row_loop(BcpContext& ctx,
                     std::span<ColumnBinding*> fixed,
                     std::span<ColumnBinding*> string,
                     std::span<uint8_t> staging,
                     int64_t num_rows,
                     bool any_has_nulls) {
    if (!any_has_nulls)
        row_loop_fast(ctx, fixed, string, staging, num_rows);
    else
        row_loop_general(ctx, fixed, string, staging, num_rows);
}

// ── process_batch ───────────────────────────────────────────────────────────

/// Full bind or fast rebind, then row loop.
inline void process_batch(BcpContext& ctx,
                          const std::shared_ptr<arrow::RecordBatch>& batch,
                          BatchBindingState& state) {
    if (!batch || batch->num_rows() == 0 || batch->num_columns() == 0) return;

    ++ctx.record_batches;
    ctx.processed_rows += batch->num_rows();

    if (state.matches(batch->schema())) {
        // Fast rebind: skip bcp_bind, update Arrow pointers only
        rebind_columns(state.bindings, state.classified, batch);
    } else {
        // Full bind: first batch or schema changed
        state.bindings = bind_columns(ctx.bcp, ctx.dbc, batch);
        state.classified = classify_columns(state.bindings);
        state.staging = setup_staging(ctx.bcp, ctx.dbc, state.classified.fixed);
        state.schema = batch->schema();
        state.initialized = true;
    }

    row_loop(ctx, state.classified.fixed, state.classified.string,
             state.staging, batch->num_rows(), state.classified.any_has_nulls);
}

// ── finalize_bcp ────────────────────────────────────────────────────────────

/// Final flush + bcp_done. Retries bcp_batch once (50ms) on transient failure
/// before raising. Throws on bcp_done failure.
inline void finalize_bcp(BcpContext& ctx) {
    // Flush remaining rows that didn't trigger a batch flush
    if (ctx.sent_rows > 0 && ctx.rows_until_flush < ctx.batch_size) {
        auto ret = ctx.bcp.batch(ctx.dbc);
        if (ret == -1) [[unlikely]] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ret = ctx.bcp.batch(ctx.dbc);
        }
        if (ret == -1) [[unlikely]]
            odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_batch");
    }
    auto done = ctx.bcp.done(ctx.dbc);
    if (done == -1)
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_done");
}

// ── bulk_insert (single connection) ─────────────────────────────────────────

/// Single-connection BCP bulk insert: reads Arrow RecordBatches and sends rows.
///
/// @param dbc          Active ODBC connection handle with BCP enabled.
/// @param reader       Arrow RecordBatchReader supplying the data.
/// @param table        Target table name (qualified to dbo if no schema part).
/// @param batch_size   Rows between bcp_batch() commits; ≤0 defaults to 100K.
/// @param table_hint   BCP hint string (e.g. "TABLOCK"); empty to skip.
/// @return BcpMetrics with wall-clock timings and row counts.
/// @throws std::runtime_error on Arrow read failure, zero rows, or ODBC error.
[[nodiscard]] inline BcpMetrics bulk_insert(
    SQLHDBC dbc,
    std::shared_ptr<arrow::RecordBatchReader> reader,
    const std::string& table,
    int64_t batch_size,
    const std::string& table_hint)
{
    using clock = std::chrono::steady_clock;
    const auto t_start = clock::now();

    const auto& api = ensure_bcp_api();
    auto qualified = sql::qualify_table(table);
    init_session(api, dbc, qualified, table_hint);

    BcpSessionGuard session_guard(api, dbc);

    BcpContext ctx{
        .bcp             = api,
        .dbc             = dbc,
        .batch_size      = batch_size > 0 ? batch_size : 100000LL,
    };
    ctx.rows_until_flush = ctx.batch_size;

    BatchBindingState binding_state;
    BcpMetrics metrics;
    auto t_bind_start = clock::now();

    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = reader->ReadNext(&batch);
        if (!st.ok())
            throw std::runtime_error("Failed reading Arrow batch: " + st.ToString());
        if (!batch) break;

        auto t0 = clock::now();
        process_batch(ctx, batch, binding_state);
        auto t1 = clock::now();

        // Accumulate row_loop time (includes bind within process_batch)
        metrics.row_loop_seconds += std::chrono::duration<double>(t1 - t0).count();
    }

    if (ctx.processed_rows == 0)
        throw std::runtime_error("Arrow BCP received zero rows from payload");

    auto t_flush_start = clock::now();
    session_guard.dismiss();
    finalize_bcp(ctx);
    auto t_flush_end = clock::now();

    metrics.batch_flush_seconds = std::chrono::duration<double>(t_flush_end - t_flush_start).count();
    metrics.processed_rows = ctx.processed_rows;
    metrics.sent_rows      = ctx.sent_rows;
    metrics.record_batches = ctx.record_batches;
    metrics.total_seconds  = std::chrono::duration<double>(clock::now() - t_start).count();
    return metrics;
}

// ── bulk_insert_parallel (multi-worker) ─────────────────────────────────────

/// Multi-worker parallel BCP: N threads, each with its own ODBC connection.
///
/// Reads all RecordBatches into memory, slices large batches for parallelism
/// (zero-copy via Arrow Slice), partitions across workers by greedy
/// least-loaded row count, then spawns std::thread workers.
///
/// @param conn_str     ODBC connection string (each worker gets its own conn).
/// @param reader       Arrow RecordBatchReader supplying the data.
/// @param table        Target table name (qualified to dbo if no schema part).
/// @param batch_size   Rows between bcp_batch() commits; ≤0 defaults to 100K.
/// @param table_hint   BCP hint string (e.g. "TABLOCK"); empty to skip.
/// @param num_workers  Desired worker count; ≤0 auto-selects min(4, hw_concurrency).
///                     Falls back to single-connection if only 1 worker needed.
/// @return BcpMetrics  (row_loop/batch_flush = max across workers; counts = sum).
/// @throws std::runtime_error on Arrow read failure, zero rows, or first worker error.
[[nodiscard]] inline BcpMetrics bulk_insert_parallel(
    const std::string& conn_str,
    std::shared_ptr<arrow::RecordBatchReader> reader,
    const std::string& table,
    int64_t batch_size,
    const std::string& table_hint,
    int num_workers)
{
    using clock = std::chrono::steady_clock;
    const auto t_start = clock::now();

    // 1. Read all RecordBatches into memory
    std::vector<std::shared_ptr<arrow::RecordBatch>> all_batches;
    int64_t total_rows = 0;
    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = reader->ReadNext(&batch);
        if (!st.ok())
            throw std::runtime_error("Failed reading Arrow batch: " + st.ToString());
        if (!batch) break;
        total_rows += batch->num_rows();
        all_batches.push_back(std::move(batch));
    }

    if (all_batches.empty() || total_rows == 0)
        throw std::runtime_error("Arrow BCP received zero rows from payload");

    // 2. Slice large batches (Polars exports 1 batch) — zero-copy
    const int desired = (num_workers <= 0)
        ? std::min(4, static_cast<int>(std::thread::hardware_concurrency()))
        : num_workers;

    if (desired > 1
        && all_batches.size() < static_cast<size_t>(desired)) {
        std::vector<std::shared_ptr<arrow::RecordBatch>> sliced;
        for (auto& batch : all_batches) {
            const int64_t n = batch->num_rows();
            if (n <= 1) {
                sliced.push_back(std::move(batch));
                continue;
            }
            const int parts = std::min(desired, static_cast<int>(n));
            const int64_t chunk = n / parts;
            int64_t offset = 0;
            for (int p = 0; p < parts; ++p) {
                const int64_t len = (p < parts - 1) ? chunk : (n - offset);
                sliced.push_back(batch->Slice(offset, len));
                offset += len;
            }
        }
        all_batches = std::move(sliced);
    }

    // 3. Resolve worker count
    int max_workers = static_cast<int>(std::min<size_t>(
        std::thread::hardware_concurrency(),
        all_batches.size()));
    max_workers = std::max(max_workers, 1);

    int actual_workers = (num_workers <= 0)
        ? std::min(4, max_workers)
        : std::min(num_workers, max_workers);

    // 4. Fallback to single-connection if only 1 worker
    if (actual_workers <= 1) {
        auto table_obj = arrow::Table::FromRecordBatches(all_batches);
        if (!table_obj.ok())
            throw std::runtime_error("Failed to reassemble Arrow table: "
                                     + table_obj.status().ToString());
        auto single_reader = std::make_shared<arrow::TableBatchReader>(**table_obj);

        BcpConnectionPool pool(conn_str, 1);
        return bulk_insert(pool[0].dbc, std::move(single_reader),
                           table, batch_size, table_hint);
    }

    // 5. Partition batches by row count (greedy least-loaded)
    std::vector<std::vector<std::shared_ptr<arrow::RecordBatch>>>
        partitions(static_cast<size_t>(actual_workers));
    std::vector<int64_t> worker_rows(static_cast<size_t>(actual_workers), 0);

    for (auto& batch : all_batches) {
        auto min_it = std::min_element(worker_rows.begin(), worker_rows.end());
        auto idx = static_cast<size_t>(
            std::distance(worker_rows.begin(), min_it));
        partitions[idx].push_back(batch);
        worker_rows[idx] += batch->num_rows();
    }

    // 6. Create connection pool (parallel establishment)
    auto t_connect_start = clock::now();
    BcpConnectionPool pool(conn_str, actual_workers);
    auto t_connect_end = clock::now();

    const auto& api = ensure_bcp_api();
    auto qualified = sql::qualify_table(table);
    const int64_t effective_batch = batch_size > 0 ? batch_size : 100000LL;

    // 7. Spawn worker threads
    struct WorkerResult {
        BcpMetrics         metrics{};
        std::exception_ptr error{};
    };
    std::vector<WorkerResult> results(static_cast<size_t>(actual_workers));
    std::vector<std::thread>  threads;
    threads.reserve(static_cast<size_t>(actual_workers));

    for (int w = 0; w < actual_workers; ++w) {
        threads.emplace_back([&, w]() {
            auto& res = results[static_cast<size_t>(w)];
            auto& conn = pool[w];
            auto& my_batches = partitions[static_cast<size_t>(w)];

            try {
                const auto wt_start = clock::now();

                init_session(api, conn.dbc, qualified, table_hint);
                BcpSessionGuard guard(api, conn.dbc);

                BcpContext ctx{
                    .bcp        = api,
                    .dbc        = conn.dbc,
                    .batch_size = effective_batch,
                };
                ctx.rows_until_flush = ctx.batch_size;

                BatchBindingState state;
                auto t_loop_start = clock::now();
                for (auto& batch : my_batches)
                    process_batch(ctx, batch, state);
                auto t_loop_end = clock::now();

                auto t_flush_start = clock::now();
                guard.dismiss();
                finalize_bcp(ctx);
                auto t_flush_end = clock::now();

                res.metrics.row_loop_seconds     = std::chrono::duration<double>(t_loop_end - t_loop_start).count();
                res.metrics.batch_flush_seconds   = std::chrono::duration<double>(t_flush_end - t_flush_start).count();
                res.metrics.processed_rows        = ctx.processed_rows;
                res.metrics.sent_rows             = ctx.sent_rows;
                res.metrics.record_batches        = ctx.record_batches;
                res.metrics.total_seconds         = std::chrono::duration<double>(clock::now() - wt_start).count();
            } catch (...) {
                res.error = std::current_exception();
            }
        });
    }

    // 8. Join all workers
    for (auto& t : threads)
        t.join();

    // 9. Check for errors (rethrow first encountered)
    for (int w = 0; w < actual_workers; ++w) {
        if (results[static_cast<size_t>(w)].error) {
            try {
                std::rethrow_exception(results[static_cast<size_t>(w)].error);
            } catch (const std::exception& e) {
                const auto& wm = results[static_cast<size_t>(w)].metrics;
                throw std::runtime_error(
                    std::string("Parallel BCP worker ") + std::to_string(w)
                    + "/" + std::to_string(actual_workers)
                    + " failed (sent " + std::to_string(wm.sent_rows)
                    + "/" + std::to_string(worker_rows[static_cast<size_t>(w)])
                    + " rows): " + e.what());
            }
        }
    }

    // 10. Merge metrics
    BcpMetrics merged{};
    merged.connect_seconds = std::chrono::duration<double>(t_connect_end - t_connect_start).count();
    for (int w = 0; w < actual_workers; ++w)
        merged.merge_parallel(results[static_cast<size_t>(w)].metrics);
    merged.total_seconds = std::chrono::duration<double>(clock::now() - t_start).count();
    return merged;
}

} // namespace pygim::strategy::mssql::bcp
