// persistence/strategy/mssql/parallel_load.h
// Parallel range-partitioned load: N workers, each fetching a slice of the table.
// Primary connection = worker 0. N-1 fresh connections for workers 1..N-1.
// Each worker: SQLFetch block cursor → ArrowBuilder → Table.
// Final: ConcatenateTables for zero-copy merge.

#pragma once

#include "backend.h"
#include "fetch_buffer.h"
#include "load_cache.h"
#include "load_connection_pool.h"
#include "load_dispatch.h"
#include "odbc_error.h"
#include "schema_describe.h"
#include "sql_type_map.h"
#include "stmt_handle.h"

#include "../../core/arrow_builder.h"
#include "../../core/load_result.h"
#include "../../../utils/logging.h"

#include <arrow/table.h>
#include <arrow/result.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pygim::strategy::mssql {

namespace detail {

/// Result of MIN/MAX metadata query.
struct RangeInfo {
    int64_t min_val;
    int64_t max_val;
};

/// Per-worker result: Arrow Table + metrics slice.
struct WorkerResult {
    std::shared_ptr<arrow::Table> table;
    core::LoadMetrics metrics;
    std::exception_ptr error;
};

/// Query MIN and MAX of the partition column on the given connection.
/// Returns RangeInfo. Throws on error.
[[nodiscard]]
inline RangeInfo query_min_max(OdbcConnection& conn,
                               std::string_view table_name,
                               std::string_view partition_column) {
    // Build SQL: SELECT MIN([col]), MAX([col]) FROM [schema].[table]
    MssqlDialect dialect;
    auto quoted_col = dialect.quote_identifier(partition_column);
    auto quoted_table = dialect.quote_table_name(table_name);
    auto sql = std::format("SELECT MIN({}), MAX({}) FROM {}",
                           quoted_col, quoted_col, quoted_table);

    PYGIM_LOG_FMT("[parallel_load] min/max query: %s\n", sql.c_str());

    StmtHandle stmt(conn.dbc());

    SQLRETURN ret = SQLExecDirect(
        stmt,
        const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(sql.c_str())),
        SQL_NTS);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "query_min_max: SQLExecDirect");

    SQLLEN ind_min = 0, ind_max = 0;
    int64_t min_val = 0, max_val = 0;

    ret = SQLBindCol(stmt, 1, SQL_C_SBIGINT, &min_val, sizeof(min_val), &ind_min);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "query_min_max: BindCol MIN");

    ret = SQLBindCol(stmt, 2, SQL_C_SBIGINT, &max_val, sizeof(max_val), &ind_max);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "query_min_max: BindCol MAX");

    ret = SQLFetch(stmt);
    odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "query_min_max: SQLFetch");

    // Empty table check
    if (ind_min == SQL_NULL_DATA || ind_max == SQL_NULL_DATA) {
        return {0, -1};  // Signals empty — caller should handle
    }

    PYGIM_LOG_FMT("[parallel_load] range: min=%lld, max=%lld\n",
                  static_cast<long long>(min_val), static_cast<long long>(max_val));

    return {min_val, max_val};
}

/// Generate per-worker SQL with WHERE range clause.
[[nodiscard]]
inline std::vector<std::string> generate_worker_queries(
    std::string_view table_name,
    std::string_view partition_column,
    int64_t min_val, int64_t max_val,
    int num_workers) {
    MssqlDialect dialect;
    auto quoted_col = dialect.quote_identifier(partition_column);
    auto quoted_table = dialect.quote_table_name(table_name);

    int64_t range = max_val - min_val + 1;
    int64_t chunk = range / num_workers;
    if (chunk < 1) chunk = 1;

    std::vector<std::string> queries;
    queries.reserve(static_cast<std::size_t>(num_workers));

    for (int w = 0; w < num_workers; ++w) {
        int64_t lo = min_val + static_cast<int64_t>(w) * chunk;
        int64_t hi = (w == num_workers - 1)
                       ? max_val
                       : (min_val + static_cast<int64_t>(w + 1) * chunk - 1);

        queries.push_back(std::format(
            "SELECT * FROM {} WHERE {} >= {} AND {} <= {}",
            quoted_table, quoted_col, lo, quoted_col, hi));
    }

    return queries;
}

// SchemaInfo and describe_columns() are in schema_describe.h

/// Single worker fetch loop. Reuses shared schema info.
/// Returns WorkerResult with table and metrics.
inline void run_worker(OdbcConnection& conn,
                       std::string_view sql,
                       SchemaInfo const& schema_info,
                       int64_t block_size,
                       WorkerResult& out) {
    using clock = std::chrono::steady_clock;
    auto& metrics = out.metrics;
    try {
        StmtHandle stmt(conn.dbc());

        // Prepare + Execute
        {
            const auto t0 = clock::now();
            SQLRETURN ret = SQLPrepare(
                stmt, const_cast<SQLCHAR*>(
                          reinterpret_cast<const SQLCHAR*>(sql.data())),
                static_cast<SQLINTEGER>(sql.size()));
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "worker: SQLPrepare");

            ret = SQLExecute(stmt);
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "worker: SQLExecute");
            metrics.prepare_seconds = std::chrono::duration<double>(clock::now() - t0).count();
        }

        // Setup builder + buffers + dispatch using shared schema
        core::ArrowBuilder builder(schema_info.schema);
        auto buffers = FetchBufferSet::allocate(schema_info.col_info, block_size);
        auto dispatch = build_column_dispatch(schema_info.schema, schema_info.nullable_flags);

        // Configure block cursor
        {
            SQLRETURN ret = SQLSetStmtAttr(
                stmt, SQL_ATTR_ROW_ARRAY_SIZE,
                reinterpret_cast<SQLPOINTER>(block_size), 0);
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                                 "worker: SQLSetStmtAttr(ROW_ARRAY_SIZE)");

            ret = SQLSetStmtAttr(
                stmt, SQL_ATTR_ROWS_FETCHED_PTR,
                &buffers.rows_fetched, 0);
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                                 "worker: SQLSetStmtAttr(ROWS_FETCHED_PTR)");
        }

        // Bind columns
        buffers.bind(stmt);

        // Fetch loop
        const std::size_t ncols = schema_info.col_info.size();
        std::vector<uint8_t> valid;

        while (true) {
            SQLRETURN ret;
            {
                const auto t0 = clock::now();
                ret = SQLFetch(stmt);
                metrics.fetch_seconds +=
                    std::chrono::duration<double>(clock::now() - t0).count();
            }

            if (ret == SQL_NO_DATA) break;
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "worker: SQLFetch");

            const int64_t nrows = static_cast<int64_t>(buffers.rows_fetched);

            const auto t_build = clock::now();
            for (std::size_t c = 0; c < ncols; ++c) {
                auto& col = buffers.columns[c];
                const auto& cd = dispatch[c];

                const uint8_t* valid_ptr = nullptr;
                if (cd.nullable) {
                    valid.resize(static_cast<std::size_t>(nrows));
                    indicators_to_valid_bytes(col.indicators.data(), nrows, valid.data());
                    valid_ptr = valid.data();
                }

                cd.fn(builder, c, col, nrows, valid_ptr);
            }

            metrics.build_seconds +=
                std::chrono::duration<double>(clock::now() - t_build).count();
            metrics.fetched_rows += nrows;
            metrics.fetched_blocks++;
        }

        out.table = builder.finish();
        metrics.columns = static_cast<int64_t>(ncols);

    } catch (...) {
        out.error = std::current_exception();
    }
}

} // namespace detail

/// Execute a parallel range-partitioned load.
///
/// @param conn              Primary connection (becomes worker 0)
/// @param table_name        Table to load from (schema-qualified ok)
/// @param partition_column  Column to range-partition on (integer type)
/// @param load_workers      Number of parallel workers (>= 2)
/// @param block_size        Block cursor size per worker
/// @param load_cache        Persistent pool cache for parallel load workers
/// @param packet_size       ODBC packet size for parallel load connections
[[nodiscard]]
inline core::LoadResult execute_parallel(
    OdbcConnection& conn,
    std::string_view table_name,
    std::string_view partition_column,
    int load_workers,
    int64_t block_size,
    MssqlLoadCache& load_cache,
    int packet_size = 16384) {
    using clock = std::chrono::steady_clock;
    core::LoadMetrics metrics;
    const auto t_total = clock::now();

    PYGIM_LOG_FMT("[parallel_load] starting: table=%.*s, partition=%.*s, workers=%d\n",
                  static_cast<int>(table_name.size()), table_name.data(),
                  static_cast<int>(partition_column.size()), partition_column.data(),
                  load_workers);

    // 1. Query MIN/MAX on primary connection
    auto range = detail::query_min_max(conn, table_name, partition_column);

    // Empty table → return empty result
    if (range.max_val < range.min_val) {
        MssqlDialect dialect;
        auto sql = std::format("SELECT * FROM {} WHERE 1=0",
                               dialect.quote_table_name(table_name));
        StmtHandle stmt(conn.dbc());
        SQLRETURN ret = SQLExecDirect(
            stmt, const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(sql.c_str())),
            SQL_NTS);
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "parallel_load: empty schema query");
        auto schema_info = describe_columns(stmt);
        core::ArrowBuilder builder(schema_info.schema);
        return core::LoadResult{builder.finish(), metrics};
    }

    // Adjust workers if range is too small
    int64_t total_range = range.max_val - range.min_val + 1;
    if (total_range < load_workers) {
        load_workers = static_cast<int>(total_range);
        PYGIM_LOG_FMT("[parallel_load] adjusted workers to %d (range=%lld)\n",
                      load_workers, static_cast<long long>(total_range));
    }
    if (load_workers < 2) {
        load_workers = 1;
        PYGIM_LOG_FMT("[parallel_load] range too small for parallel — running single-worker\n");
    }

    // 2. Generate per-worker queries
    auto queries = detail::generate_worker_queries(
        table_name, partition_column,
        range.min_val, range.max_val, load_workers);

    // 3. Describe schema on primary connection using worker 0's query
    //    MSSQL ODBC supports SQLDescribeCol after SQLPrepare — no execute needed.
    SchemaInfo schema_info;
    {
        const auto t0 = clock::now();
        StmtHandle stmt(conn.dbc());
        SQLRETURN ret = SQLPrepare(
            stmt, const_cast<SQLCHAR*>(
                      reinterpret_cast<const SQLCHAR*>(queries[0].c_str())),
            static_cast<SQLINTEGER>(queries[0].size()));
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "parallel_load: SQLPrepare schema");
        schema_info = describe_columns(stmt);
        metrics.describe_seconds = std::chrono::duration<double>(clock::now() - t0).count();
        metrics.columns = static_cast<int64_t>(schema_info.col_info.size());
    }

    // 4. Establish N-1 worker connections (cached)
    auto run_workers = [&](MssqlLoadCache& cache) -> std::vector<detail::WorkerResult> {
        const auto t_connect = clock::now();
        auto* extra_pool = cache.ensure_pool(conn.conn_str(), load_workers - 1, packet_size);
        metrics.connect_seconds = std::chrono::duration<double>(clock::now() - t_connect).count();
        PYGIM_LOG_FMT("[parallel_load] %d extra connections in %.3fs\n",
                      load_workers - 1, metrics.connect_seconds);

        std::vector<detail::WorkerResult> results(static_cast<std::size_t>(load_workers));

        {
            std::vector<std::thread> threads;
            threads.reserve(static_cast<std::size_t>(load_workers - 1));

            // Workers 1..N-1 on extra connections
            for (int w = 1; w < load_workers; ++w) {
                threads.emplace_back([&, w]() {
                    detail::run_worker((*extra_pool)[w - 1],
                                       queries[static_cast<std::size_t>(w)],
                                       schema_info, block_size,
                                       results[static_cast<std::size_t>(w)]);
                });
            }

            // Worker 0 on primary connection (this thread)
            detail::run_worker(conn, queries[0], schema_info, block_size, results[0]);

            for (auto& t : threads) t.join();
        }

        return results;
    };

    // 5. First attempt
    auto results = run_workers(load_cache);

    // Check for connection errors — retry once with fresh connections
    bool has_connection_error = false;
    for (auto const& r : results) {
        if (r.error) {
            try {
                std::rethrow_exception(r.error);
            } catch (std::runtime_error const& e) {
                std::string_view msg(e.what());
                if (msg.contains("08S01") || msg.contains("08001") ||
                    msg.contains("HY000") || msg.contains("Communication link")) {
                    has_connection_error = true;
                    break;
                }
                // Non-connection error — rethrow immediately
                throw;
            }
        }
    }

    if (has_connection_error) {
        PYGIM_LOG_FMT("[parallel_load] stale connection detected — retrying with fresh connections\n");
        load_cache.clear();
        results = run_workers(load_cache);
        for (auto const& r : results) {
            if (r.error) std::rethrow_exception(r.error);
        }
    } else {
        for (auto const& r : results) {
            if (r.error) std::rethrow_exception(r.error);
        }
    }

    // 6. Concatenate tables
    const auto t_concat = clock::now();
    std::vector<std::shared_ptr<arrow::Table>> tables;
    tables.reserve(static_cast<std::size_t>(load_workers));
    for (auto& r : results) {
        if (r.table && r.table->num_rows() > 0) {
            tables.push_back(std::move(r.table));
        }
    }

    std::shared_ptr<arrow::Table> combined;
    if (tables.empty()) {
        core::ArrowBuilder builder(schema_info.schema);
        combined = builder.finish();
    } else if (tables.size() == 1) {
        combined = std::move(tables[0]);
    } else {
        auto concat_result = arrow::ConcatenateTables(tables);
        if (!concat_result.ok()) {
            throw std::runtime_error(
                std::string("parallel_load: ConcatenateTables failed: ") +
                concat_result.status().ToString());
        }
        combined = std::move(*concat_result);
    }
    metrics.concat_seconds = std::chrono::duration<double>(clock::now() - t_concat).count();

    // 7. Aggregate metrics
    for (auto const& r : results) {
        metrics.fetch_seconds = std::max(metrics.fetch_seconds, r.metrics.fetch_seconds);
        metrics.build_seconds = std::max(metrics.build_seconds, r.metrics.build_seconds);
        metrics.prepare_seconds = std::max(metrics.prepare_seconds, r.metrics.prepare_seconds);
        metrics.fetched_rows += r.metrics.fetched_rows;
        metrics.fetched_blocks += r.metrics.fetched_blocks;
    }
    metrics.workers_used = load_workers;
    metrics.total_seconds = std::chrono::duration<double>(clock::now() - t_total).count();

    PYGIM_LOG_FMT("[parallel_load] done: %lld rows, %d workers, %.3fs total "
                  "(connect=%.3f fetch=%.3f build=%.3f concat=%.3f)\n",
                  static_cast<long long>(metrics.fetched_rows),
                  load_workers, metrics.total_seconds,
                  metrics.connect_seconds, metrics.fetch_seconds,
                  metrics.build_seconds, metrics.concat_seconds);

    return core::LoadResult{std::move(combined), metrics};
}

} // namespace pygim::strategy::mssql
