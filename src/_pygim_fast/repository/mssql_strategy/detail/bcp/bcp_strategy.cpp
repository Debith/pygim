// BCP strategy orchestrator — pure C++ entry point, ZERO pybind11 dependency.
// Delegates to modular headers:
//   1. BCP API loading  →  bcp_api.h
//   2. Column binding   →  bcp_bind.h
//   3. Row loop         →  bcp_row_loop.h
// The py::object → RecordBatchReader conversion lives in the BINDING layer
// (bcp_arrow_import.h), not here.

#include "../../mssql_strategy.h"
#include "../helpers.h"
#include "../../../../utils/logging.h"
#include "../../../../utils/quick_timer.h"

#include "bcp_api.h"
#include "bcp_types.h"
#include "bcp_bind.h"
#include "bcp_row_loop.h"

#define PYGIM_HAVE_ARROW 1
#define PYGIM_HAVE_ODBC  1

namespace pygim {
namespace {

// ── Session setup helpers ───────────────────────────────────────────────────

void enable_bcp_attr(SQLHDBC dbc) {
    SQLULEN status = SQL_BCP_OFF;
    auto rc = SQLGetConnectAttr(dbc, SQL_COPT_SS_BCP, &status, 0, nullptr);
    if (SQL_SUCCEEDED(rc) && status == SQL_BCP_ON) return;

    rc = SQLSetConnectAttr(dbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_UINTEGER);
    if (!SQL_SUCCEEDED(rc))
        MssqlStrategyNative::raise_if_error(rc, SQL_HANDLE_DBC, dbc, "SQLSetConnectAttr(BCP_ON)");
}

std::string qualify_table(const std::string& table) {
    auto ok = [](const std::string& s) {
        if (detail::is_valid_identifier(s)) return true;
        auto dot = s.find('.');
        if (dot == std::string::npos) return false;
        return !s.substr(0, dot).empty() && !s.substr(dot + 1).empty()
            && detail::is_valid_identifier(s.substr(0, dot))
            && detail::is_valid_identifier(s.substr(dot + 1));
    };
    if (!ok(table)) throw std::runtime_error("Invalid table identifier");
    return (table.find('.') == std::string::npos) ? "dbo." + table : table;
}

void init_session(const bcp::BcpApi& api, SQLHDBC dbc,
                  const std::string& qualified_table,
                  const std::string& hint) {
    std::u16string w;
    w.reserve(qualified_table.size());
    for (char ch : qualified_table) w.push_back(static_cast<char16_t>(ch));

    if (api.init(dbc, reinterpret_cast<LPCWSTR>(w.c_str()),
                 nullptr, nullptr, DB_IN) != bcp::kSucceed) {
        MssqlStrategyNative::raise_if_error(
            SQL_ERROR, SQL_HANDLE_DBC, dbc,
            ("bcp_init(table=" + qualified_table + ")").c_str());
    }
    if (!hint.empty() && api.control) {
        if (api.control(dbc, bcp::kBcpHints,
                        const_cast<char*>(hint.c_str())) != bcp::kSucceed) {
            MssqlStrategyNative::raise_if_error(
                SQL_ERROR, SQL_HANDLE_DBC, dbc, "bcp_control(BCPHINTS)");
        }
    }
}

// ── Finalize: flush remaining rows + bcp_done ───────────────────────────────

void finalize_bcp(bcp::BcpContext& ctx) {
    if (ctx.sent_rows > 0 && ctx.sent_rows % ctx.batch_size != 0) {
        ctx.timer.start_sub_timer("batch_flush", false);
        auto ret = ctx.bcp.batch(ctx.dbc);
        ctx.timer.stop_sub_timer("batch_flush", false);
        if (ret == -1)
            MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_batch");
    }
    ctx.timer.start_sub_timer("done", false);
    auto done = ctx.bcp.done(ctx.dbc);
    ctx.timer.stop_sub_timer("done", false);
    if (done == -1)
        MssqlStrategyNative::raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, ctx.dbc, "bcp_done");
}

// ── Extract timing metrics ──────────────────────────────────────────────────

void extract_metrics(const bcp::BcpContext& ctx,
                     const QuickTimer& timer,
                     MssqlStrategyNative::BcpMetrics& m) {
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

// ── Public entry point (pure C++) ───────────────────────────────────────────

void MssqlStrategyNative::bulk_insert_arrow_bcp(
    const std::string& table,
    std::shared_ptr<arrow::RecordBatchReader> reader,
    const std::string& input_mode,
    int batch_size,
    const std::string& table_hint)
{
    PYGIM_SCOPE_LOG_TAG("repo.bcp");
#if PYGIM_HAVE_ODBC && PYGIM_HAVE_ARROW
    m_last_bcp_metrics = BcpMetrics{};
    QuickTimer timer("bulk_insert_arrow_bcp", std::clog, false, false);

    // 1. Setup: load driver, enable BCP, init session
    timer.start_sub_timer("setup", false);
    const auto& api = bcp::ensure_bcp_api();
    ensure_connected();
    enable_bcp_attr(m_dbc);
    auto qualified = qualify_table(table);
    init_session(api, m_dbc, qualified, table_hint);
    timer.stop_sub_timer("setup", false);

    // 2. Iterate RecordBatchReader — no Python references past this point
    bcp::BcpContext ctx{api, m_dbc, timer,
                        batch_size > 0 ? static_cast<int64_t>(batch_size) : 100000LL};

    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = reader->ReadNext(&batch);
        if (!st.ok())
            throw std::runtime_error("Failed reading Arrow batch: " + st.ToString());
        if (!batch) break;
        bcp::process_batch(ctx, batch);
    }
    m_last_bcp_metrics.input_mode = input_mode;

    // 3. Finalize
    if (ctx.processed_rows == 0)
        throw std::runtime_error("Arrow BCP received zero rows from payload");

    finalize_bcp(ctx);
    extract_metrics(ctx, timer, m_last_bcp_metrics);
#elif PYGIM_HAVE_ODBC
    throw std::runtime_error(
        "bulk_insert_arrow_bcp requires Arrow C++ library (not detected at build time)");
#else
    throw std::runtime_error(
        "MssqlStrategyNative built without ODBC headers; feature unavailable");
#endif
}

} // namespace pygim
