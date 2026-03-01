// BCP strategy orchestrator — pure C++ entry point, ZERO pybind11 dependency.
// Delegates to modular headers:
//   1. BCP API loading  →  bcp_api.h
//   2. Column binding   →  bcp_bind.h
//   3. Row loop         →  bcp_row_loop.h
// The py::object → RecordBatchReader conversion lives in the BINDING layer
// (bcp_arrow_import.h), not here.

// mssql_strategy_v2.h MUST be included first: it includes core/value_types.h
// which uses BOOL as an enum member. ODBC headers (pulled in by bcp_*.h and
// odbc_error.h) define BOOL as a macro, so they must come AFTER value_types.h.
#include "../../mssql_strategy_v2.h"

#include "../odbc_error.h"
#include "../sql_helpers.h"
#include "../../../../utils/logging.h"
#include "../../../../utils/quick_timer.h"

#include "bcp_api.h"
#include "bcp_types.h"
#include "bcp_bind.h"
#include "bcp_row_loop.h"

namespace pygim::bcp {
namespace {

// ── Session setup helpers ───────────────────────────────────────────────────

void enable_bcp_attr(SQLHDBC dbc) {
    SQLULEN status = SQL_BCP_OFF;
    auto rc = SQLGetConnectAttr(dbc, SQL_COPT_SS_BCP, &status, 0, nullptr);
    if (SQL_SUCCEEDED(rc) && status == SQL_BCP_ON) return;

    rc = SQLSetConnectAttr(dbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_UINTEGER);
    if (!SQL_SUCCEEDED(rc))
        odbc::raise_if_error(rc, SQL_HANDLE_DBC, dbc, "SQLSetConnectAttr(BCP_ON)");
}

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
    if (ctx.sent_rows > 0 && ctx.sent_rows % ctx.batch_size != 0) {
        ctx.timer.start_sub_timer("batch_flush", false);
        auto ret = ctx.bcp.batch(ctx.dbc);
        ctx.timer.stop_sub_timer("batch_flush", false);
        if (ret == -1)
            odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_batch");
    }
    ctx.timer.start_sub_timer("done", false);
    auto done = ctx.bcp.done(ctx.dbc);
    ctx.timer.stop_sub_timer("done", false);
    if (done == -1)
        odbc::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_done");
}

// ── Extract timing metrics ──────────────────────────────────────────────────

void extract_metrics(const BcpContext& ctx,
                     const QuickTimer& timer,
                     mssql::BcpMetrics& m) {
    auto sub = [&](std::string_view name) -> double {
        try { return timer.sub_timer_seconds(name); }
        catch (...) { return 0.0; }
    };
    m.setup_seconds        = sub("setup");
    m.reader_open_seconds  = sub("reader_open");
    m.bind_columns_seconds = sub("bind_columns");
    m.row_loop_seconds     = sub("row_loop");
    m.batch_flush_seconds  = sub("batch_flush");
    m.done_seconds         = sub("done");
    m.processed_rows       = ctx.processed_rows;
    m.sent_rows            = ctx.sent_rows;
    m.record_batches       = ctx.record_batches;
    m.total_seconds        = timer.total_seconds();
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
    mssql::BcpMetrics& metrics_out)
{
    PYGIM_SCOPE_LOG_TAG("repo.bcp");
    metrics_out = mssql::BcpMetrics{};
    QuickTimer timer("bulk_insert_arrow_bcp", std::clog, false, false);

    // 1. Setup: load driver, enable BCP, init session
    timer.start_sub_timer("setup", false);
    const auto& api = ensure_bcp_api();
    enable_bcp_attr(dbc);
    auto qualified = sql::qualify_table(table);
    init_session(api, dbc, qualified, table_hint);
    timer.stop_sub_timer("setup", false);

    // 2. Iterate RecordBatchReader — no Python references past this point
    BcpContext ctx{api, dbc, timer,
                   batch_size > 0 ? static_cast<int64_t>(batch_size) : 100000LL};

    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = reader->ReadNext(&batch);
        if (!st.ok())
            throw std::runtime_error("Failed reading Arrow batch: " + st.ToString());
        if (!batch) break;
        process_batch(ctx, batch);
    }
    metrics_out.input_mode = input_mode;

    // 3. Finalize
    if (ctx.processed_rows == 0)
        throw std::runtime_error("Arrow BCP received zero rows from payload");

    finalize_bcp(ctx);
    extract_metrics(ctx, timer, metrics_out);
}

} // namespace pygim::bcp
