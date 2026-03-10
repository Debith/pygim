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

#include <algorithm>
#include <cctype>
#include <cstdlib>

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

} // namespace pygim::bcp
