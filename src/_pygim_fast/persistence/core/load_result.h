// repository/core/load_result.h
// LoadMetrics and LoadResult — pure data structs for the load pipeline.
// No logic, no dependencies beyond Arrow table pointer.

#pragma once

#include <arrow/table.h>
#include <cstdint>
#include <memory>

namespace pygim::core {

/// Timing and volumetric metrics captured during a single load operation.
struct LoadMetrics {
    double  total_seconds{0};
    double  prepare_seconds{0};     // SQLPrepare + SQLExecute
    double  describe_seconds{0};    // SQLDescribeCol loop
    double  fetch_seconds{0};       // all SQLFetch calls (cumulative)
    double  build_seconds{0};       // Arrow builder append + finish
    double  connect_seconds{0};     // parallel connection establishment
    double  concat_seconds{0};      // ConcatenateTables time
    int64_t fetched_rows{0};
    int64_t fetched_blocks{0};      // number of SQLFetch calls
    int64_t columns{0};
    int     workers_used{1};        // actual parallelism level
};

/// Holds the materialised Arrow table and the metrics that describe how it was produced.
struct LoadResult {
    std::shared_ptr<arrow::Table>  table;
    LoadMetrics                    metrics;
};

} // namespace pygim::core
