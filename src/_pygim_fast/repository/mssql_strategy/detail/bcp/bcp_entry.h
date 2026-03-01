#pragma once
// Public BCP entry point — forward declaration for callers.
// Implementation lives in bcp/bcp_strategy.cpp.

#include <memory>
#include <string>

// Forward-declare Arrow types.
namespace arrow { class RecordBatchReader; }

// Forward-declare BcpMetrics.
namespace pygim::mssql { struct BcpMetrics; }

#include <sql.h>
#include <sqlext.h>

namespace pygim::bcp {

/// Perform bulk insert via BCP using an Arrow RecordBatchReader.
/// This is a free function — decoupled from any strategy class.
/// @param dbc           ODBC connection handle (already connected).
/// @param table         Target table name (optionally schema-qualified).
/// @param reader        Arrow RecordBatchReader supplying rows.
/// @param input_mode    Descriptive label for metrics (e.g., "arrow", "polars").
/// @param batch_size    BCP batch size (0 = default 100k).
/// @param table_hint    Optional BCP hint string.
/// @param metrics_out   Populated with timing/row-count metrics on success.
void bulk_insert_arrow_bcp(
    SQLHDBC dbc,
    const std::string& table,
    std::shared_ptr<arrow::RecordBatchReader> reader,
    const std::string& input_mode,
    int batch_size,
    const std::string& table_hint,
    mssql::BcpMetrics& metrics_out);

} // namespace pygim::bcp
