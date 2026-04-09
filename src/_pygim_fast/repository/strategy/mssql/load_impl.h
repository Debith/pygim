// repository/strategy/mssql/load_impl.h
// MssqlLoadImpl — ODBC block-cursor fetch → Arrow Table.
//
// Drives ArrowBuilder via SQLFetch with SQL_ATTR_ROW_ARRAY_SIZE.
// No intermediate ResultSet.  Returns LoadResult (table + metrics).

#pragma once

#include "backend.h"
#include "fetch_buffer.h"
#include "load_dispatch.h"
#include "odbc_error.h"
#include "parallel_load.h"
#include "pk_detect.h"
#include "schema_describe.h"
#include "sql_type_map.h"
#include "stmt_handle.h"

#include "../../core/arrow_builder.h"
#include "../../core/load_result.h"
#include "../../../utils/logging.h"

#include <arrow/type.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// SQLLEN must be 64-bit signed — required by the SQLLEN* → int64_t* casts
// in append_strings / append_binary.
static_assert(sizeof(SQLLEN) == 8 && std::is_signed_v<SQLLEN>,
              "SQLLEN must be 64-bit signed for zero-copy indicator passing");

namespace pygim::strategy::mssql {

/// MssqlLoadImpl — Block-cursor load strategy for SQL Server.
///
/// Executes a SQL query via ODBC, fetches results in blocks, and
/// materialises them into an Arrow Table through ArrowBuilder.
struct MssqlLoadImpl {
    static constexpr int64_t kDefaultBlockSize = 8192;

    /// Execute a SQL query and return results as Arrow Table + metrics.
    ///
    /// @param conn              Active ODBC connection.
    /// @param sql               SQL query string to execute.
    /// @param load_workers      Number of parallel load connections.
    ///                          1 = single-connection block cursor;
    ///                          >1 = parallel range-partitioned load.
    /// @param partition_column  Column to range-partition on (integer type).
    /// @param table_name        Table name for parallel load (needed for MIN/MAX).
    /// @param load_cache        Persistent pool cache for parallel load workers.
    /// @param block_size        Block cursor size (rows per fetch).
    /// @param packet_size       ODBC packet size for parallel load connections.
    [[nodiscard]]
    static core::LoadResult execute(OdbcConnection& conn,
                                    std::string_view sql,
                                    int load_workers,
                                    std::string_view partition_column,
                                    std::string_view table_name,
                                    MssqlLoadCache& load_cache,
                                    int64_t block_size = kDefaultBlockSize,
                                    int packet_size = kDefaultPacketSize) {
        using clock = std::chrono::steady_clock;
        core::LoadMetrics metrics;
        const auto t_total = clock::now();

        PYGIM_LOG_FMT("[MssqlLoadImpl] execute(sql=\"%.*s\", workers=%d)\n",
                      static_cast<int>(sql.size()), sql.data(), load_workers);

        if (load_workers > 1) {
            if (!table_name.empty()) {
                std::string resolved_partition(partition_column);
                if (resolved_partition.empty()) {
                    resolved_partition = detect_partition_column(conn.dbc(), table_name);
                }
                if (!resolved_partition.empty()) {
                    return execute_parallel(conn, table_name, resolved_partition,
                                            load_workers, block_size, load_cache,
                                            packet_size);
                }
                PYGIM_LOG_FMT("[MssqlLoadImpl] no integer PK found for '%.*s' "
                              "— falling back to single-threaded\n",
                              static_cast<int>(table_name.size()), table_name.data());
            } else {
                PYGIM_LOG_FMT("[MssqlLoadImpl] raw SQL with parallel load "
                              "— falling back to single-threaded\n");
            }
        }

        // ── 1. Allocate statement handle ────────────────────────
        StmtHandle stmt(conn.dbc());

        // ── 2. Prepare + execute ────────────────────────────────
        {
            const auto t0 = clock::now();

            SQLRETURN ret = SQLPrepare(
                stmt, const_cast<SQLCHAR*>(
                          reinterpret_cast<const SQLCHAR*>(sql.data())),
                static_cast<SQLINTEGER>(sql.size()));
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare");

            ret = SQLExecute(stmt);
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLExecute");

            metrics.prepare_seconds =
                std::chrono::duration<double>(clock::now() - t0).count();
        }

        // ── 3. Describe result columns → schema ────────────────
        SchemaInfo schema_info;
        {
            const auto t0 = clock::now();
            schema_info = describe_columns(stmt);
            metrics.columns = static_cast<int64_t>(schema_info.col_info.size());
            metrics.describe_seconds =
                std::chrono::duration<double>(clock::now() - t0).count();
        }

        // ── 4. Set up builder + fetch buffers + dispatch ──────
        core::ArrowBuilder builder(schema_info.schema);
        auto buffers = FetchBufferSet::allocate(schema_info.col_info, block_size);
        auto dispatch = build_column_dispatch(schema_info.schema, schema_info.nullable_flags);

        // ── 5. Configure block cursor ───────────────────────────────
        {
            SQLRETURN ret = SQLSetStmtAttr(
                stmt, SQL_ATTR_ROW_ARRAY_SIZE,
                reinterpret_cast<SQLPOINTER>(block_size), 0);
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                                 "SQLSetStmtAttr(ROW_ARRAY_SIZE)");

            ret = SQLSetStmtAttr(
                stmt, SQL_ATTR_ROWS_FETCHED_PTR,
                &buffers.rows_fetched, 0);
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                                 "SQLSetStmtAttr(ROWS_FETCHED_PTR)");
        }

        // ── 6. Bind columns ────────────────────────────────────
        buffers.bind(stmt);

        // ── 7. Block-cursor fetch loop ──────────────────────────
        const std::size_t ncols = schema_info.col_info.size();
        std::vector<uint8_t> valid;   // reusable validity buffer

        while (true) {
            // -- fetch block --
            SQLRETURN ret;
            {
                const auto t0 = clock::now();
                ret = SQLFetch(stmt);
                metrics.fetch_seconds +=
                    std::chrono::duration<double>(clock::now() - t0).count();
            }

            if (ret == SQL_NO_DATA) break;
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLFetch");

            const int64_t nrows =
                static_cast<int64_t>(buffers.rows_fetched);

            // -- append to Arrow builder --
            const auto t_build = clock::now();

            for (std::size_t c = 0; c < ncols; ++c) {
                auto& col = buffers.columns[c];
                const auto& cd = dispatch[c];

                // Only compute validity when column can actually be null
                const uint8_t* valid_ptr = nullptr;
                if (cd.nullable) {
                    valid.resize(static_cast<std::size_t>(nrows));
                    indicators_to_valid_bytes(col.indicators.data(), nrows,
                                              valid.data());
                    valid_ptr = valid.data();
                }

                // O(1) dispatch — no branches
                cd.fn(builder, c, col, nrows, valid_ptr);
            }

            metrics.build_seconds +=
                std::chrono::duration<double>(clock::now() - t_build).count();
            metrics.fetched_rows += nrows;
            metrics.fetched_blocks++;
        }

        // ── 8. Finalize ────────────────────────────────────────
        auto table = builder.finish();
        metrics.workers_used = 1;
        metrics.total_seconds =
            std::chrono::duration<double>(clock::now() - t_total).count();

        PYGIM_LOG_FMT("[MssqlLoadImpl] done: %lld rows, %lld blocks, "
                      "%.3fs total (prep=%.3f desc=%.3f fetch=%.3f "
                      "build=%.3f)\n",
                      static_cast<long long>(metrics.fetched_rows),
                      static_cast<long long>(metrics.fetched_blocks),
                      metrics.total_seconds, metrics.prepare_seconds,
                      metrics.describe_seconds, metrics.fetch_seconds,
                      metrics.build_seconds);

        return core::LoadResult{std::move(table), metrics};
    }
};

// NOTE: static_assert(BackendPolicy<MssqlBackend>) deferred to bindings.cpp
// where all types (MssqlSaveImpl, MssqlLoadImpl) are fully defined.

} // namespace pygim::strategy::mssql
