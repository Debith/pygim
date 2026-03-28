// repository/core/save_impl.h
// Core C++ package — MssqlSaveImpl placeholder.
//
// Compile-time dispatch: ArrowTable → BCP bulk insert.
// Supports single-connection and parallel multi-connection BCP.

#pragma once

#include "backend_trait.h"

#include "../../utils/logging.h"
#include <string_view>

namespace pygim::core {

struct MssqlSaveImpl {
    // Placeholder: in real code this receives arrow::Table
    // and the connection, then does BCP bulk insert.
    static void execute(OdbcConnection& conn,
                        std::string_view table_name,
                        int bcp_workers = 1) {
        PYGIM_LOG_FMT("[MssqlSaveImpl] execute(table=\"%.*s\", workers=%d)\n",
                      static_cast<int>(table_name.size()), table_name.data(),
                      bcp_workers);

        if (bcp_workers < 2) {
            PYGIM_LOG_FMT("[MssqlSaveImpl]   single-connection BCP path\n");
            PYGIM_LOG_FMT("[MssqlSaveImpl]   bcp_init(\"%.*s\", TABLOCK)\n",
                          static_cast<int>(table_name.size()), table_name.data());
            PYGIM_LOG_FMT("[MssqlSaveImpl]   SQLBindCol per column from Arrow schema\n");
            PYGIM_LOG_FMT("[MssqlSaveImpl]   transpose → bcp_sendrow loop\n");
            PYGIM_LOG_FMT("[MssqlSaveImpl]   bcp_batch() at intervals\n");
            PYGIM_LOG_FMT("[MssqlSaveImpl]   bcp_done() → final commit\n");
        } else {
            PYGIM_LOG_FMT("[MssqlSaveImpl]   parallel BCP path (%d workers)\n",
                          bcp_workers);
            PYGIM_LOG_FMT("[MssqlSaveImpl]   read all RecordBatches from reader\n");
            PYGIM_LOG_FMT("[MssqlSaveImpl]   zero-copy Slice into %d sub-batches\n",
                          bcp_workers);
            PYGIM_LOG_FMT("[MssqlSaveImpl]   BcpConnectionPool(%d connections)\n",
                          bcp_workers);
            PYGIM_LOG_FMT("[MssqlSaveImpl]   spawn %d std::thread workers\n",
                          bcp_workers);
            PYGIM_LOG_FMT("[MssqlSaveImpl]   each worker: bcp_init → transpose → "
                          "sendrow → bcp_done\n");
            PYGIM_LOG_FMT("[MssqlSaveImpl]   join threads, merge metrics\n");
        }

        PYGIM_LOG_FMT("[MssqlSaveImpl]   done\n");
    }
};

} // namespace pygim::core
