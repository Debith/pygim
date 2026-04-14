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

static py::object acquire_datastore(const std::string& conn_str,
                               const std::string& format,
                               std::size_t pool_size,
                               int64_t batch_size,
                               const std::string& table_hint,
                               int bcp_workers,
                               int64_t block_size,
                               int packet_size) {
    auto fmt = adapter::parse_format(format);
    return py::cast(MssqlRepo::create(conn_str, fmt, pool_size,
                                      batch_size, table_hint, bcp_workers,
                                      block_size, packet_size));
}

PYBIND11_MODULE(_repository, m) {
    m.doc() = "DataStore — database access with Arrow core and format conversion";

    // Bridge C++ std::runtime_error to GimError (RuntimeError subclass).
    // This aligns with the project's GimError exception hierarchy while
    // maintaining backward compatibility with existing except RuntimeError catches.
    static auto gim_error = py::exception<std::runtime_error>(m, "GimError", PyExc_RuntimeError);

    // Format enum exposed to Python
    py::enum_<adapter::Format>(m, "Format")
        .value("polars", adapter::Format::Polars)
        .value("pandas", adapter::Format::Pandas)
        .export_values();

    // ONE class, not two
    py::class_<MssqlRepo>(m, "DataStore")
        .def("save", &MssqlRepo::save,
             py::arg("data"), py::arg("table_name"), py::arg("bcp_workers") = -1,
             "Bulk-insert data into a table via BCP. Returns a dict of timing metrics.")
        .def("load",
             py::overload_cast<std::string_view, int, std::string_view>(&MssqlRepo::load),
             py::arg("source"), py::arg("load_workers") = 1,
             py::arg("partition_column") = "",
             "Load data from a table name or raw SQL query. Returns a DataFrame.\n"
             "When load_workers > 1 and partition_column is empty, the integer\n"
             "primary key column is auto-detected via ODBC metadata.")
        .def("load",
             py::overload_cast<core::Query const&, int, std::string_view>(&MssqlRepo::load),
             py::arg("query"), py::arg("load_workers") = 1,
             py::arg("partition_column") = "",
             "Load data from a Query object. Returns a DataFrame.\n"
             "When load_workers > 1 and partition_column is empty, the integer\n"
             "primary key column is auto-detected via ODBC metadata.")
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
    m.def("acquire_datastore", &acquire_datastore,
          py::arg("conn_str"),
          py::arg("format") = "polars",
          py::arg("pool_size") = 4,
          py::arg("batch_size") = 100000,
          py::arg("table_hint") = "TABLOCK",
          py::arg("bcp_workers") = 1,
          py::arg("block_size") = 4096,
          py::arg("packet_size") = 16384,
          R"doc(
          Create a DataStore from a connection string.

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
          block_size : int
              Block cursor size for load operations (default: 4096).
          packet_size : int
              ODBC connection packet size (default: 16384).
          )doc");
}
