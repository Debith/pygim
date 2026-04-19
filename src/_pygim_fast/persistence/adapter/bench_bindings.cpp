// persistence/adapter/bench_bindings.cpp
// Standalone pybind11 module for ODBC fetch throughput benchmarks.
// Separated from production _persistence module to keep the main library lean.

#include "../strategy/mssql/backend.h"
#include "../strategy/mssql/fetch_benchmark.h"
#include "../strategy/mssql/sql_helpers.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

static py::dict run_fetch_benchmark(const std::string& conn_str,
                                    const std::string& table_name,
                                    const std::vector<int64_t>& block_sizes) {
    namespace mssql = pygim::strategy::mssql;

    auto qualified = mssql::sql::qualify_table(table_name);
    auto sql = std::string("SELECT * FROM ") + qualified;

    py::dict results;

    for (auto bs : block_sizes) {
        mssql::OdbcConnection conn;
        conn.open(conn_str);

        mssql::FetchBenchResult r;
        {
            py::gil_scoped_release release;
            r = mssql::bench_fetch(conn, sql, bs);
        }

        py::dict entry;
        entry["block_size"]      = r.block_size;
        entry["total_rows"]      = r.total_rows;
        entry["total_blocks"]    = r.total_blocks;
        entry["num_columns"]     = r.num_columns;
        entry["prepare_s"]       = r.prepare_s;
        entry["describe_bind_s"] = r.describe_bind_s;
        entry["fetch_s"]         = r.fetch_s;
        entry["total_s"]         = r.total_s;
        entry["rows_per_s"]      = (r.fetch_s > 0.0)
                                     ? r.total_rows / r.fetch_s : 0.0;

        results[py::int_(bs)] = entry;
    }

    return results;
}

static py::dict run_parallel_fetch_benchmark(
    const std::string& conn_str,
    const std::string& table_name,
    int64_t total_rows,
    const std::vector<int>& worker_counts,
    int64_t block_size) {
    namespace mssql = pygim::strategy::mssql;

    auto qualified = mssql::sql::qualify_table(table_name);

    py::dict results;

    for (auto nw : worker_counts) {
        mssql::ParallelFetchResult r;
        {
            py::gil_scoped_release release;
            r = mssql::bench_fetch_parallel(conn_str, qualified, total_rows, nw, block_size);
        }

        py::dict entry;
        entry["num_workers"]  = r.num_workers;
        entry["total_rows"]   = r.total_rows;
        entry["block_size"]   = r.block_size;
        entry["total_s"]      = r.total_s;
        entry["fetch_s"]      = r.fetch_s;
        entry["rows_per_s"]   = (r.total_s > 0.0) ? r.total_rows / r.total_s : 0.0;

        results[py::int_(nw)] = entry;
    }

    return results;
}

PYBIND11_MODULE(_fetch_benchmark, m) {
    m.doc() = "Raw ODBC fetch throughput benchmarks (not part of production API)";

    m.def("fetch_benchmark", &run_fetch_benchmark,
          py::arg("conn_str"), py::arg("table_name"), py::arg("block_sizes"),
          R"doc(
          Raw SQLFetch throughput benchmark at various block sizes.
          Measures pure ODBC download speed with no Arrow conversion.

          Parameters
          ----------
          conn_str : str
              ODBC connection string.
          table_name : str
              Table to SELECT * FROM (will be schema-qualified).
          block_sizes : list[int]
              Block sizes (row array sizes) to benchmark.

          Returns
          -------
          dict
              Keyed by block_size, values are dicts of timing metrics.
          )doc");

    m.def("parallel_fetch_benchmark", &run_parallel_fetch_benchmark,
          py::arg("conn_str"), py::arg("table_name"),
          py::arg("total_rows"), py::arg("worker_counts"),
          py::arg("block_size") = 512,
          "Parallel range-partitioned SQLFetch benchmark.");
}
