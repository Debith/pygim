// repository/adapter/test_bindings.cpp
// Test-only pybind11 bindings for repository internals.
//
// Compiled as pygim._repository_test, NOT included in production builds.
// Exposes Query builder and other core internals for black-box testing.

#include "../core/query.h"
#include "../core/dialect.h"
#include "../strategy/mssql/dialect.h"
#include "../strategy/mssql/save_impl.h"
#include "../strategy/mssql/load_impl.h"
#include "adapter.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace py = pybind11;
using namespace pygim;

using MssqlRepo = adapter::RepositoryAdapter<strategy::mssql::MssqlBackend>;

PYBIND11_MODULE(_repository_test, m) {
    m.doc() = "Test-only bindings for repository internals";

    // Query builder — needed for testing structured queries
    py::class_<core::Query>(m, "Query", py::module_local())
        .def(py::init<>())
        .def(py::init<std::string_view>(), py::arg("raw_sql"))
        .def("select", &core::Query::select, py::arg("col"),
             py::return_value_policy::reference_internal)
        .def("from_table", &core::Query::from_table, py::arg("table"),
             py::return_value_policy::reference_internal)
        .def("where", &core::Query::where, py::arg("clause"),
             py::return_value_policy::reference_internal)
        .def("limit", &core::Query::limit, py::arg("n"),
             py::return_value_policy::reference_internal)
        .def("is_raw", &core::Query::is_raw)
        .def_property_readonly("table", [](core::Query const& q) {
            return std::string(q.table());
        })
        .def_property_readonly("columns", &core::Query::columns)
        .def_property_readonly("where_clause", [](core::Query const& q) {
            return std::string(q.where_clause());
        })
        .def_property_readonly("limit_value", [](core::Query const& q) -> py::object {
            if (q.limit_value()) return py::int_(*q.limit_value());
            return py::none();
        })
        .def_property_readonly("raw_sql", [](core::Query const& q) {
            return std::string(q.raw_sql());
        });

    // MssqlDialect — needed for testing SQL rendering
    py::class_<strategy::mssql::MssqlDialect>(m, "MssqlDialect", py::module_local())
        .def(py::init<>())
        .def("render", &strategy::mssql::MssqlDialect::render, py::arg("query"))
        .def("quote_identifier", &strategy::mssql::MssqlDialect::quote_identifier,
             py::arg("name"));

    // Format enum (module-local to avoid conflict with production _repository)
    py::enum_<adapter::Format>(m, "Format", py::module_local())
        .value("polars", adapter::Format::Polars)
        .value("pandas", adapter::Format::Pandas)
        .export_values();

    // RepositoryAdapter — module-local to avoid conflict with production _repository
    py::class_<MssqlRepo>(m, "Repository", py::module_local())
        .def(py::init([](const std::string& conn_str, const std::string& format,
                         std::size_t pool_size, int64_t batch_size,
                         const std::string& table_hint, int bcp_workers) {
            auto fmt = adapter::parse_format(format);
            return MssqlRepo::create(conn_str, fmt, pool_size,
                                     batch_size, table_hint, bcp_workers);
        }),
             py::arg("conn_str"),
             py::arg("format") = "polars",
             py::arg("pool_size") = 4,
             py::arg("batch_size") = 100000,
             py::arg("table_hint") = "TABLOCK",
             py::arg("bcp_workers") = 1)
        .def("save", &MssqlRepo::save,
             py::arg("data"), py::arg("table_name"), py::arg("bcp_workers") = -1)
        .def("load",
             py::overload_cast<std::string_view, int>(&MssqlRepo::load),
             py::arg("source"), py::arg("load_workers") = 1)
        .def("load",
             py::overload_cast<core::Query const&, int>(&MssqlRepo::load),
             py::arg("query"), py::arg("load_workers") = 1)
        .def("add_pre_transform", &MssqlRepo::add_pre_transform,
             py::arg("fn"))
        .def("add_post_transform", &MssqlRepo::add_post_transform,
             py::arg("fn"))
        .def("clear_transforms", &MssqlRepo::clear_transforms)
        .def_property_readonly("format",
             [](MssqlRepo const& r) { return r.format(); })
        .def("__repr__", &MssqlRepo::repr);
}
