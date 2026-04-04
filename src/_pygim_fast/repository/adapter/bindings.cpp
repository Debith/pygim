// repository/adapter/bindings.cpp
// Production pybind11 bindings for the _repository module.
//
// Binds ONE class: RepositoryAdapter<MssqlBackend>.
// Format is runtime enum, not template parameter — single instantiation.
// Test-specific bindings (Query, pool internals) live in test_bindings.cpp.
// Fetch benchmarks live in bench_bindings.cpp (_fetch_benchmark module).

#include "../core/query.h"
#include "../strategy/mssql/save_impl.h"
#include "../strategy/mssql/load_impl.h"
#include "adapter.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace py = pybind11;
using namespace pygim;

static_assert(core::BackendPolicy<strategy::mssql::MssqlBackend>,
              "MssqlBackend must satisfy BackendPolicy");

using MssqlRepo = adapter::RepositoryAdapter<strategy::mssql::MssqlBackend>;

static py::object acquire_repo(const std::string& conn_str,
                               const std::string& format,
                               std::size_t pool_size,
                               int64_t batch_size,
                               const std::string& table_hint,
                               int bcp_workers) {
    auto fmt = adapter::parse_format(format);
    return py::cast(MssqlRepo::create(conn_str, fmt, pool_size,
                                      batch_size, table_hint, bcp_workers));
}

PYBIND11_MODULE(_repository, m) {
    m.doc() = "Repository — database access with Arrow core and format conversion";

    // Format enum exposed to Python
    py::enum_<adapter::Format>(m, "Format")
        .value("polars", adapter::Format::Polars)
        .value("pandas", adapter::Format::Pandas)
        .export_values();

    // ONE class, not two
    py::class_<MssqlRepo>(m, "Repository")
        .def("save", &MssqlRepo::save,
             py::arg("data"), py::arg("table_name"), py::arg("bcp_workers") = -1,
             "Bulk-insert data into a table via BCP. Returns a dict of timing metrics.")
        .def("load",
             py::overload_cast<std::string_view, int>(&MssqlRepo::load),
             py::arg("source"), py::arg("load_workers") = 1,
             "Load data from a table name or raw SQL query. Returns a DataFrame.")
        .def("load",
             py::overload_cast<core::Query const&, int>(&MssqlRepo::load),
             py::arg("query"), py::arg("load_workers") = 1,
             "Load data from a Query object. Returns a DataFrame.")
        .def("add_pre_transform", &MssqlRepo::add_pre_transform,
             py::arg("fn"),
             "Add a callable invoked before each save/load operation.")
        .def("add_post_transform", &MssqlRepo::add_post_transform,
             py::arg("fn"),
             "Add a callable invoked after each save/load operation.")
        .def("clear_transforms", &MssqlRepo::clear_transforms,
             "Remove all pre and post transform hooks.")
        .def_property_readonly("format",
             [](MssqlRepo const& r) { return r.format(); })
        .def("__repr__", &MssqlRepo::repr);

    // Factory function
    m.def("acquire_repo", &acquire_repo,
          py::arg("conn_str"),
          py::arg("format") = "polars",
          py::arg("pool_size") = 4,
          py::arg("batch_size") = 100000,
          py::arg("table_hint") = "TABLOCK",
          py::arg("bcp_workers") = 1,
          R"doc(
          Create a repository from a connection string.

          Parameters
          ----------
          conn_str : str
              Connection string.
          format : str
              Output format: "polars" (default) or "pandas".
          pool_size : int
              Maximum pooled connections (default: 4).
          batch_size : int
              BCP batch size (default: 100000).
          table_hint : str
              BCP table hint (default: "TABLOCK").
          bcp_workers : int
              Number of parallel BCP workers (default: 1).
          )doc");
}
