// persistence/strategy/mssql/save_impl.h
// MssqlSaveImpl — BCP bulk insert via Arrow Table.
//
// Delegates to bcp::bulk_insert (single-connection) or
// bcp::bulk_insert_parallel (multi-worker) from bcp_pipeline.h.
// Keyed writes (upsert / insert-missing) stage via BCP into a local temp
// table on the same connection, then MERGE / anti-join INSERT (upsert.h).
// Returns BcpMetrics so the adapter can report timing to Python.

#pragma once

#include "backend.h"
#include "bcp/bcp_pipeline.h"
#include "stmt_handle.h"
#include "upsert.h"

#include "../../../utils/logging.h"
#include <arrow/table.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::strategy::mssql {

/// Result of a keyed save: BCP staging metrics plus the rows the final
/// MERGE / INSERT statement affected on the target table.
struct KeyedSaveResult {
    bcp::BcpMetrics metrics;
    long long       affected_rows{0};
};

/// MssqlSaveImpl — BCP bulk insert implementation for SQL Server.
/// Routes to single-connection or parallel path based on bcp_workers.
struct MssqlSaveImpl {
    static bcp::BcpMetrics execute(
        OdbcConnection& conn,
        std::shared_ptr<arrow::Table> table_data,
        std::string_view table_name,
        int64_t batch_size,
        const std::string& table_hint,
        int bcp_workers)
    {
        std::string table(table_name);

        PYGIM_LOG_FMT("[MssqlSaveImpl] execute(table=\"%s\", workers=%d, batch_size=%lld)\n",
                      table.c_str(), bcp_workers, static_cast<long long>(batch_size));

        if (bcp_workers < 2) {
            return bcp::bulk_insert(conn.dbc(), table_data,
                                    table, batch_size, table_hint);
        } else {
            return bcp::bulk_insert_parallel(conn.conn_str(), table_data,
                                             table, batch_size, table_hint,
                                             bcp_workers);
        }
    }

    /// Keyed write: BCP into #staging, then one atomic set-based statement
    /// against the target. Single-connection by construction — the local
    /// temp table is only visible on the connection that created it.
    ///
    /// Duplicate merge-key values in the frame fail fast (server-side THROW
    /// before the final statement). If the target has an IDENTITY column
    /// present in the frame, the final statement runs under IDENTITY_INSERT
    /// and the identity column is excluded from MERGE updates.
    ///
    /// @param update_matched  true → MERGE (update on key match, insert
    ///     otherwise); false → anti-join INSERT (skip existing keys).
    [[nodiscard]] static KeyedSaveResult execute_keyed(
        OdbcConnection& conn,
        std::shared_ptr<arrow::Table> table_data,
        std::string_view table_name,
        int64_t batch_size,
        bool update_matched,
        const std::vector<std::string>& keys)
    {
        std::vector<std::string> columns;
        columns.reserve(static_cast<std::size_t>(table_data->num_columns()));
        for (const auto& field : table_data->schema()->fields()) {
            columns.push_back(field->name());
        }
        upsert::validate_columns(columns, keys);

        const MssqlDialect dialect{};
        const auto qualified = sql::qualify_table(std::string(table_name));
        const auto target_quoted = dialect.quote_table_name(qualified);
        const auto stage = upsert::next_stage_name();

        PYGIM_LOG_FMT("[MssqlSaveImpl] execute_keyed(table=\"%s\", stage=\"%s\", mode=%s)\n",
                      qualified.c_str(), stage.c_str(),
                      update_matched ? "upsert" : "insert_missing");

        // Identity probe: inserting explicit values into an IDENTITY column
        // (the most common upsert key) needs IDENTITY_INSERT around the
        // final statement; qualified passed identifier validation above.
        const std::string identity_col = query_scalar_string(
            conn.dbc(),
            "SELECT name FROM sys.columns WHERE object_id = OBJECT_ID(N'" +
                qualified + "') AND is_identity = 1");
        const bool frame_has_identity =
            !identity_col.empty() &&
            std::ranges::find(columns, identity_col) != columns.end();

        exec_direct(conn.dbc(),
                    upsert::render_create_stage(stage, target_quoted, columns, dialect));

        KeyedSaveResult result;
        try {
            result.metrics = bcp::bulk_insert(conn.dbc(), table_data, stage,
                                              batch_size, "TABLOCK");
            exec_direct(conn.dbc(),
                        upsert::render_duplicate_guard(stage, keys, dialect));
            std::string statement = update_matched
                ? upsert::render_merge(target_quoted, stage, columns, keys, dialect,
                                       frame_has_identity ? identity_col : std::string{})
                : upsert::render_insert_missing(target_quoted, stage, columns, keys, dialect);
            if (frame_has_identity) {
                statement = "SET IDENTITY_INSERT " + target_quoted + " ON; " + statement +
                            " SET IDENTITY_INSERT " + target_quoted + " OFF;";
            }
            result.affected_rows = exec_direct(conn.dbc(), statement);
            exec_direct(conn.dbc(), "DROP TABLE " + stage + ";");
        } catch (...) {
            // Pooled connections outlive this call: never leak the staging
            // table onto a reused connection.
            try { exec_direct(conn.dbc(), "DROP TABLE " + stage + ";"); } catch (...) {}
            throw;
        }
        return result;
    }
};

} // namespace pygim::strategy::mssql
