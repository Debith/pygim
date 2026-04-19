// repository/strategy/mssql/fetch_benchmark.h
// Temporary benchmark — measures raw SQLFetch throughput at various block sizes.
// Strips all Arrow/conversion overhead to find the ODBC download floor.

#pragma once

#include "backend.h"
#include "fetch_buffer.h"
#include "odbc_error.h"
#include "sql_type_map.h"
#include "stmt_handle.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pygim::strategy::mssql {

struct FetchBenchResult {
    int64_t block_size{};
    int64_t total_rows{};
    int64_t total_blocks{};
    int64_t num_columns{};
    double  prepare_s{};
    double  describe_bind_s{};
    double  fetch_s{};      // Pure SQLFetch loop only
    double  total_s{};
};

/// Raw SQLFetch throughput benchmark for a single block size.
/// Fetches all data and discards it — measures pure ODBC download speed.
[[nodiscard]]
inline FetchBenchResult bench_fetch(OdbcConnection& conn,
                                    std::string_view sql,
                                    int64_t block_size) {
    using clock = std::chrono::steady_clock;
    FetchBenchResult r;
    r.block_size = block_size;
    const auto t_total = clock::now();

    // 1. Prepare + execute
    StmtHandle stmt(conn.dbc());
    {
        const auto t0 = clock::now();
        SQLRETURN ret = SQLPrepare(
            stmt,
            const_cast<SQLCHAR*>(
                reinterpret_cast<const SQLCHAR*>(sql.data())),
            static_cast<SQLINTEGER>(sql.size()));
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "bench: SQLPrepare");

        ret = SQLExecute(stmt);
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "bench: SQLExecute");
        r.prepare_s = std::chrono::duration<double>(clock::now() - t0).count();
    }

    // 2. Describe columns + allocate buffers + bind
    //    Keep buffers alive through the fetch loop (SQLBindCol references their memory).
    {
        const auto t0 = clock::now();

        SQLSMALLINT num_cols = 0;
        SQLNumResultCols(stmt, &num_cols);
        r.num_columns = num_cols;

        std::vector<std::pair<std::string, TypeMapping>> col_info;
        col_info.reserve(static_cast<std::size_t>(num_cols));
        for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(num_cols); ++i) {
            SQLCHAR col_name[256];
            SQLSMALLINT name_len = 0, sql_type = 0, dec_digits = 0, nullable = 0;
            SQLULEN col_size = 0;
            SQLDescribeCol(stmt, i, col_name, sizeof(col_name),
                           &name_len, &sql_type, &col_size, &dec_digits, &nullable);
            auto mapping = resolve_type(sql_type, col_size, dec_digits);
            col_info.emplace_back(
                std::string(reinterpret_cast<const char*>(col_name),
                            static_cast<std::size_t>(name_len)),
                std::move(mapping));
        }

        auto buffers = FetchBufferSet::allocate(col_info, block_size);

        // Configure block cursor
        SQLRETURN ret = SQLSetStmtAttr(
            stmt, SQL_ATTR_ROW_ARRAY_SIZE,
            reinterpret_cast<SQLPOINTER>(block_size), 0);
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "bench: ROW_ARRAY_SIZE");

        ret = SQLSetStmtAttr(
            stmt, SQL_ATTR_ROWS_FETCHED_PTR,
            &buffers.rows_fetched, 0);
        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "bench: ROWS_FETCHED_PTR");

        buffers.bind(stmt);

        r.describe_bind_s = std::chrono::duration<double>(clock::now() - t0).count();

        // 3. Pure fetch loop — discard data
        {
            const auto t_fetch = clock::now();
            while (true) {
                ret = SQLFetch(stmt);
                if (ret == SQL_NO_DATA) [[likely]] break;
                odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "bench: SQLFetch");
                r.total_rows += static_cast<int64_t>(buffers.rows_fetched);
                r.total_blocks++;
            }
            r.fetch_s = std::chrono::duration<double>(clock::now() - t_fetch).count();
        }
    }

    r.total_s = std::chrono::duration<double>(clock::now() - t_total).count();
    return r;
}

struct ParallelFetchResult {
    int num_workers{};
    int64_t total_rows{};
    int64_t block_size{};
    double  total_s{};
    double  fetch_s{};   // max fetch time across workers (determines wall-clock)
};

/// Parallel range-partitioned fetch benchmark.
/// Splits "SELECT * FROM table WHERE id >= lo AND id <= hi" across N workers.
/// Each worker opens its own ODBC connection. Assumes 'id' column with 1..total_rows.
/// Pure download speed — no Arrow building.
[[nodiscard]]
inline ParallelFetchResult bench_fetch_parallel(
    std::string_view conn_str,
    std::string_view table,     // qualified table name like "dbo.bcp_bench_exhaustive"
    int64_t total_rows,
    int num_workers,
    int64_t block_size = 512)
{
    using clock = std::chrono::steady_clock;
    ParallelFetchResult result;
    result.num_workers = num_workers;
    result.block_size = block_size;

    // Build per-worker SQL queries with range partitions
    struct WorkerTask {
        std::string sql;
        int64_t rows_fetched{0};
        double fetch_s{0};
    };

    std::vector<WorkerTask> tasks(static_cast<std::size_t>(num_workers));
    int64_t chunk = total_rows / num_workers;
    for (int w = 0; w < num_workers; ++w) {
        int64_t lo = w * chunk + 1;
        int64_t hi = (w == num_workers - 1) ? total_rows : (w + 1) * chunk;
        tasks[static_cast<std::size_t>(w)].sql =
            "SELECT * FROM " + std::string(table) +
            " WHERE id >= " + std::to_string(lo) +
            " AND id <= " + std::to_string(hi);
    }

    const auto t_total = clock::now();

    // Launch workers — each opens its own connection
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(num_workers));

    std::string conn_str_copy(conn_str);  // ensure stable storage for threads

    for (int w = 0; w < num_workers; ++w) {
        threads.emplace_back([&tasks, w, &conn_str_copy, block_size]() {
            auto& task = tasks[static_cast<std::size_t>(w)];
            OdbcConnection conn;
            conn.open(conn_str_copy);
            auto r = bench_fetch(conn, task.sql, block_size);
            task.rows_fetched = r.total_rows;
            task.fetch_s = r.fetch_s;
        });
    }

    for (auto& t : threads) t.join();

    result.total_s = std::chrono::duration<double>(clock::now() - t_total).count();

    // Aggregate
    double max_fetch = 0;
    for (auto& task : tasks) {
        result.total_rows += task.rows_fetched;
        max_fetch = std::max(max_fetch, task.fetch_s);
    }
    result.fetch_s = max_fetch;

    return result;
}

} // namespace pygim::strategy::mssql
