// repository/strategy/mssql/save_impl.h
// MssqlSaveImpl — BCP bulk insert via Arrow RecordBatchReader.
//
// Delegates to bcp::bulk_insert (single-connection) or
// bcp::bulk_insert_parallel (multi-worker) from bcp_pipeline.h.
// Returns BcpMetrics so the adapter can report timing to Python.

#pragma once

#include "backend.h"
#include "bcp/bcp_pipeline.h"

#include "../../../utils/logging.h"
#include <arrow/record_batch.h>
#include <memory>
#include <string>
#include <string_view>

namespace pygim::strategy::mssql {

/// MssqlSaveImpl — BCP bulk insert implementation for SQL Server.
/// Routes to single-connection or parallel path based on bcp_workers.
struct MssqlSaveImpl {
    static bcp::BcpMetrics execute(
        OdbcConnection& conn,
        std::shared_ptr<arrow::RecordBatchReader> reader,
        std::string_view table_name,
        int64_t batch_size,
        const std::string& table_hint,
        int bcp_workers)
    {
        std::string table(table_name);

        PYGIM_LOG_FMT("[MssqlSaveImpl] execute(table=\"%s\", workers=%d, batch_size=%lld)\n",
                      table.c_str(), bcp_workers, static_cast<long long>(batch_size));

        if (bcp_workers < 2) {
            return bcp::bulk_insert(conn.dbc(), std::move(reader),
                                    table, batch_size, table_hint);
        } else {
            return bcp::bulk_insert_parallel(conn.conn_str(), std::move(reader),
                                             table, batch_size, table_hint,
                                             bcp_workers);
        }
    }
};

} // namespace pygim::strategy::mssql
