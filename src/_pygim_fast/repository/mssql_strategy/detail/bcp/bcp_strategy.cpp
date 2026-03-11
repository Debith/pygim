// BCP strategy orchestrator — pure C++ entry point, ZERO pybind11 dependency.
// Delegates to modular headers:
//   1. BCP API loading  →  bcp_api.h
//   2. Column binding   →  bcp_bind.h
//   3. Row loop         →  bcp_row_loop.h
// The py::object → RecordBatchReader conversion lives in the BINDING layer
// (bcp_arrow_import.h), not here.

// mssql_strategy.h MUST be included first: it includes core/value_types.h
// which uses BOOL as an enum member. ODBC headers (pulled in by bcp_*.h and
// odbc_error.h) define BOOL as a macro, so they must come AFTER value_types.h.
#include "../../mssql_strategy.h"

#include "../odbc_error.h"
#include "../sql_helpers.h"
#include "../../../../utils/logging.h"
#include "../../../../utils/quick_timer.h"

#include "bcp_api.h"
#include "bcp_types.h"
#include "bcp_bind.h"
#include "bcp_row_loop.h"
#include "bcp_connection_pool.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <thread>

#include <arrow/table.h>
#include <arrow/table_builder.h>

namespace pygim::bcp {
namespace {

TimingLevel resolve_timing_level_from_env() {
    const char* raw = std::getenv("PYGIM_TIMING_LEVEL");
    if (!raw || *raw == '\0') return TimingLevel::Stage;

    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (value == "off" || value == "none" || value == "0") return TimingLevel::Off;
    if (value == "hot" || value == "deep" || value == "2") return TimingLevel::Hot;
    return TimingLevel::Stage;
}

// ── RAII guard: ensures bcp_done is called even on exception ────────────────

/// Calls bcp_done() on destruction unless dismiss()'d.  Prevents leaving the
/// ODBC connection in an undefined BCP state when an exception escapes after
/// bcp_init but before finalize_bcp.
class BcpSessionGuard {
public:
    BcpSessionGuard(const BcpApi& api, SQLHDBC dbc) noexcept
        : m_api(api), m_dbc(dbc) {}

    ~BcpSessionGuard() noexcept {
        if (!m_dismissed && m_dbc != SQL_NULL_HDBC)
            m_api.done(m_dbc);  // best-effort; ignore return in dtor
    }

    /// Call once finalize_bcp() has completed successfully.
    void dismiss() noexcept { m_dismissed = true; }

    BcpSessionGuard(const BcpSessionGuard&) = delete;
    BcpSessionGuard& operator=(const BcpSessionGuard&) = delete;

private:
    const BcpApi& m_api;
    SQLHDBC       m_dbc;
    bool          m_dismissed{false};
};

// ── Session setup helpers ───────────────────────────────────────────────────

void init_session(const BcpApi& api, SQLHDBC dbc,
                  const std::string& qualified_table,
                  const std::string& hint) {
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

// ── Finalize: flush remaining rows + bcp_done ───────────────────────────────

void finalize_bcp(BcpContext& ctx) {
    // Flush any remaining rows that didn't trigger a batch flush.
    if (ctx.sent_rows > 0 && ctx.rows_until_flush < ctx.batch_size) {
        stage_timer_start(ctx, ctx.timer_batch_flush_id, "batch_flush");
        auto ret = ctx.bcp.batch(ctx.dbc);
        stage_timer_stop(ctx, ctx.timer_batch_flush_id, "batch_flush");
        if (ret == -1)
            odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_batch");
    }
    stage_timer_start(ctx, ctx.timer_done_id, "done");
    auto done = ctx.bcp.done(ctx.dbc);
    stage_timer_stop(ctx, ctx.timer_done_id, "done");
    if (done == -1)
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_done");
}

// ── Extract timing metrics ──────────────────────────────────────────────────

void extract_metrics(const BcpContext& ctx,
                     const QuickTimer& timer,
                     mssql::BcpMetrics& m) {
    const auto snapshot = timer.report();

    m.setup_seconds        = snapshot.sub_timer_seconds("setup");
    m.reader_open_seconds  = snapshot.sub_timer_seconds("reader_open");
    m.bind_columns_seconds = snapshot.sub_timer_seconds("bind_columns");
    m.row_loop_seconds     = snapshot.sub_timer_seconds("row_loop");
    m.fixed_copy_seconds   = ctx.fixed_copy_seconds;
    m.colptr_redirect_seconds = ctx.colptr_redirect_seconds;
    m.string_pack_seconds  = ctx.string_pack_seconds;
    m.sendrow_seconds      = ctx.sendrow_seconds;
    m.batch_flush_seconds  = snapshot.sub_timer_seconds("batch_flush");
    m.done_seconds         = snapshot.sub_timer_seconds("done");
    m.processed_rows       = ctx.processed_rows;
    m.sent_rows            = ctx.sent_rows;
    m.record_batches       = ctx.record_batches;
    m.simd_level           = ctx.simd_level;
    m.timing_level         = timing_level_name(ctx.timing_level);
    m.total_seconds        = snapshot.total_seconds;
}

} // anonymous namespace

// ── Public free function (pure C++) ─────────────────────────────────────────

void bulk_insert_arrow_bcp(
    SQLHDBC dbc,
    const std::string& table,
    std::shared_ptr<arrow::RecordBatchReader> reader,
    const std::string& input_mode,
    int batch_size,
    const std::string& table_hint,
    mssql::BcpMetrics& metrics_out,
    TransposeStrategy* transpose)
{
    PYGIM_SCOPE_LOG_TAG("repo.bcp");
    metrics_out = mssql::BcpMetrics{};
    QuickTimer timer("bulk_insert_arrow_bcp", std::clog, false, false);

    // 1. Setup: load driver, init session
    //    Note: SQL_COPT_SS_BCP is already enabled by MssqlStrategy::ensure_connected()
    //    before the connection is established. No redundant check needed here.
    const auto timer_setup_id = timer.get_or_create_sub_timer_id("setup");
    timer.start_sub_timer(timer_setup_id, false);
    const auto& api = ensure_bcp_api();
    auto qualified = sql::qualify_table(table);
    init_session(api, dbc, qualified, table_hint);
    timer.stop_sub_timer(timer_setup_id, false);

    // RAII guard: if anything throws after bcp_init, bcp_done is called
    // to leave the connection in a clean state.
    BcpSessionGuard session_guard(api, dbc);

    // 2. Iterate RecordBatchReader — no Python references past this point
    BcpContext ctx{api, dbc, timer,
                   batch_size > 0 ? static_cast<int64_t>(batch_size) : 100000LL};
    ctx.rows_until_flush = ctx.batch_size;  // init decrementing counter
    ctx.timing_level = resolve_timing_level_from_env();
    ctx.timer_setup_id = timer_setup_id;
    ctx.timer_bind_columns_id = timer.get_or_create_sub_timer_id("bind_columns");
    ctx.timer_row_loop_id = timer.get_or_create_sub_timer_id("row_loop");
    ctx.timer_batch_flush_id = timer.get_or_create_sub_timer_id("batch_flush");
    ctx.timer_done_id = timer.get_or_create_sub_timer_id("done");

    if (!stage_timing_enabled(ctx)) {
        ctx.timer_setup_id = BcpContext::kInvalidTimerId;
        ctx.timer_bind_columns_id = BcpContext::kInvalidTimerId;
        ctx.timer_row_loop_id = BcpContext::kInvalidTimerId;
        ctx.timer_batch_flush_id = BcpContext::kInvalidTimerId;
        ctx.timer_done_id = BcpContext::kInvalidTimerId;
    }
    ctx.transpose = transpose;  // nullptr → run_row_loop falls back to RowMajorTranspose

    BatchBindingState binding_state;  // reused across same-schema batches

    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = reader->ReadNext(&batch);
        if (!st.ok())
            throw std::runtime_error("Failed reading Arrow batch: " + st.ToString());
        if (!batch) break;
        process_batch(ctx, batch, binding_state);
    }
    metrics_out.input_mode = input_mode;

    // 3. Finalize
    if (ctx.processed_rows == 0)
        throw std::runtime_error("Arrow BCP received zero rows from payload");

    finalize_bcp(ctx);
    session_guard.dismiss();  // finalize_bcp succeeded — suppress dtor bcp_done
    extract_metrics(ctx, timer, metrics_out);
}

// ── Parallel BCP orchestrator ───────────────────────────────────────────────

void bulk_insert_arrow_bcp_parallel(
    const std::string& conn_str,
    const std::string& table,
    std::shared_ptr<arrow::RecordBatchReader> reader,
    const std::string& input_mode,
    int batch_size,
    const std::string& table_hint,
    int num_workers,
    mssql::BcpMetrics& metrics_out,
    TransposeStrategy* transpose)
{
    PYGIM_SCOPE_LOG_TAG("repo.bcp.parallel");
    metrics_out = mssql::BcpMetrics{};
    QuickTimer timer("bulk_insert_arrow_bcp_parallel", std::clog, false, false);

    // 1. Read all RecordBatches into memory.
    const auto reader_open_id = timer.get_or_create_sub_timer_id("reader_open");
    timer.start_sub_timer(reader_open_id, false);
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
    timer.stop_sub_timer(reader_open_id, false);

    if (all_batches.empty() || total_rows == 0)
        throw std::runtime_error("Arrow BCP received zero rows from payload");

    // 1b. Slice large batches so parallelism can activate.
    //     Polars typically exports one RecordBatch for the entire DataFrame.
    //     Without slicing, max_workers is clamped to num_batches (= 1).
    //     RecordBatch::Slice is zero-copy (just offset + length adjustment).
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
            const int parts = std::min(desired,
                                       static_cast<int>(n));
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

    // 2. Resolve worker count.
    //    Auto (0): min(4, hw_concurrency, num_batches).
    //    Never more workers than batches (a worker with 0 batches would fail bcp_done).
    int max_workers = static_cast<int>(std::min<size_t>(
        std::thread::hardware_concurrency(),
        all_batches.size()));
    max_workers = std::max(max_workers, 1);

    int actual_workers = (num_workers <= 0)
        ? std::min(4, max_workers)
        : std::min(num_workers, max_workers);

    // Fallback to single-connection path if only 1 worker.
    // (Avoids pool overhead for small datasets.)
    if (actual_workers <= 1) {
        // Re-create a reader from the collected batches.
        auto table_obj = arrow::Table::FromRecordBatches(all_batches);
        if (!table_obj.ok())
            throw std::runtime_error("Failed to reassemble Arrow table: "
                                     + table_obj.status().ToString());
        auto single_reader = std::make_shared<arrow::TableBatchReader>(**table_obj);

        // Need a single connection for fallback.
        BcpConnectionPool pool(conn_str, 1);
        bulk_insert_arrow_bcp(pool[0].dbc, table, std::move(single_reader),
                              input_mode, batch_size, table_hint,
                              metrics_out, transpose);
        metrics_out.input_mode = input_mode;
        return;
    }

    // 3. Partition batches across workers (balance by row count).
    const auto timer_setup_id = timer.get_or_create_sub_timer_id("setup");
    timer.start_sub_timer(timer_setup_id, false);

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

    // 4. Create connection pool.
    BcpConnectionPool pool(conn_str, actual_workers);
    timer.stop_sub_timer(timer_setup_id, false);

    // 5. Launch worker threads.
    const auto& api = ensure_bcp_api();
    auto qualified = sql::qualify_table(table);
    const auto timing_level = resolve_timing_level_from_env();
    const int64_t effective_batch =
        batch_size > 0 ? static_cast<int64_t>(batch_size) : 100000LL;

    struct WorkerResult {
        mssql::BcpMetrics    metrics{};
        std::exception_ptr   error{};
    };
    std::vector<WorkerResult> results(static_cast<size_t>(actual_workers));
    std::vector<std::thread>  threads;
    threads.reserve(static_cast<size_t>(actual_workers));

    for (int w = 0; w < actual_workers; ++w) {
        threads.emplace_back([&, w]() {
            auto& res  = results[static_cast<size_t>(w)];
            auto& conn = pool[w];
            auto& my_batches = partitions[static_cast<size_t>(w)];

            try {
                QuickTimer wtimer("worker_" + std::to_string(w),
                                  std::clog, false, false);

                // Init BCP session on this connection.
                init_session(api, conn.dbc, qualified, table_hint);
                BcpSessionGuard guard(api, conn.dbc);

                // Build per-worker context.
                BcpContext ctx{api, conn.dbc, wtimer, effective_batch};
                ctx.rows_until_flush = ctx.batch_size;
                ctx.timing_level = timing_level;
                ctx.timer_setup_id = BcpContext::kInvalidTimerId;
                ctx.timer_bind_columns_id =
                    wtimer.get_or_create_sub_timer_id("bind_columns");
                ctx.timer_row_loop_id =
                    wtimer.get_or_create_sub_timer_id("row_loop");
                ctx.timer_batch_flush_id =
                    wtimer.get_or_create_sub_timer_id("batch_flush");
                ctx.timer_done_id =
                    wtimer.get_or_create_sub_timer_id("done");
                ctx.transpose = transpose;

                // Process this worker's partition.
                BatchBindingState state;
                for (auto& batch : my_batches)
                    process_batch(ctx, batch, state);

                finalize_bcp(ctx);
                guard.dismiss();
                extract_metrics(ctx, wtimer, res.metrics);

            } catch (...) {
                res.error = std::current_exception();
            }
        });
    }

    // 6. Join all workers.
    for (auto& t : threads)
        t.join();

    // 7. Check for errors (rethrow first encountered).
    for (int w = 0; w < actual_workers; ++w) {
        if (results[static_cast<size_t>(w)].error)
            std::rethrow_exception(results[static_cast<size_t>(w)].error);
    }

    // 8. Merge metrics from all workers.
    auto& merged = metrics_out;
    merged.input_mode = input_mode;
    merged.simd_level = results[0].metrics.simd_level;
    merged.timing_level = timing_level_name(timing_level);

    const auto snapshot = timer.report();
    merged.setup_seconds = snapshot.sub_timer_seconds("setup");
    merged.reader_open_seconds = snapshot.sub_timer_seconds("reader_open");

    for (int w = 0; w < actual_workers; ++w) {
        const auto& wm = results[static_cast<size_t>(w)].metrics;
        merged.bind_columns_seconds  += wm.bind_columns_seconds;
        merged.row_loop_seconds       = std::max(merged.row_loop_seconds, wm.row_loop_seconds);
        merged.fixed_copy_seconds    += wm.fixed_copy_seconds;
        merged.colptr_redirect_seconds += wm.colptr_redirect_seconds;
        merged.string_pack_seconds   += wm.string_pack_seconds;
        merged.sendrow_seconds        = std::max(merged.sendrow_seconds, wm.sendrow_seconds);
        merged.batch_flush_seconds    = std::max(merged.batch_flush_seconds, wm.batch_flush_seconds);
        merged.done_seconds          += wm.done_seconds;
        merged.processed_rows        += wm.processed_rows;
        merged.sent_rows             += wm.sent_rows;
        merged.record_batches        += wm.record_batches;
    }
    merged.total_seconds = snapshot.total_seconds;
}

} // namespace pygim::bcp
