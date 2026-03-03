// Pybind11 bindings for the core/adapter Repository architecture.
//
// Exposes:
// - QueryAdapter as "Query" (fluent builder)
// - Repository (adapter wrapping RepositoryCore)
//
// MemoryStrategy, MssqlStrategy, and MssqlDialect are internal C++ types —
// they are constructed/used automatically by Repository and do not need
// Python-level bindings.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../core/value_types.h"
#include "data_extractor.h"
#include "query_adapter.h"
#include "repository.h"

namespace py = pybind11;

PYBIND11_MODULE(_repository, m) {
    m.doc() = "Repository core/adapter — pybind-free strategies, "
              "centralized data extraction, dialect-based query rendering.";

    using namespace pygim;

    // ---- QueryAdapter as "Query" --------------------------------------------

    py::class_<adapter::QueryAdapter>(m, "Query")
        .def(py::init<>(),
             "Create an empty query builder.")
        .def(py::init<std::string, std::vector<py::object>>(),
             py::arg("sql"), py::arg("params") = std::vector<py::object>{},
             "Create a query from raw SQL and optional parameters.")
        .def("select", &adapter::QueryAdapter::select, py::arg("columns"),
             "Set the SELECT column list.")
        .def("from_table", &adapter::QueryAdapter::from_table, py::arg("table"),
             "Set the FROM table.")
        .def("where", &adapter::QueryAdapter::where,
             py::arg("clause"), py::arg("param"),
             "Append a WHERE clause with a bound parameter.")
        .def("limit", &adapter::QueryAdapter::limit, py::arg("rows"),
             "Set a LIMIT (<=0 clears).")
        .def("build", &adapter::QueryAdapter::build,
             py::return_value_policy::move,
             "Freeze builder state into a new Query copy.")
        .def("clone", &adapter::QueryAdapter::clone,
             py::return_value_policy::move,
             "Deep copy without rebuilding.")
        .def("params", &adapter::QueryAdapter::params_py,
             "Return bound parameters as a Python list.");

    // ---- Repository ---------------------------------------------------------

    py::class_<adapter::Repository>(m, "Repository", py::dynamic_attr())
        .def(py::init<std::string, bool>(),
             py::arg("connection_uri"),
             py::arg("transformers") = false,
             "Create a Repository from a connection URI.\n\n"
             "Supported schemes:\n"
             "  memory://              — in-memory (dev/test)\n"
             "  mssql://server/db     — MSSQL via ODBC\n"
             "  Driver={...};Server=… — raw ODBC (MSSQL)\n\n"
             "Parameters:\n"
             "  connection_uri: URI or ODBC connection string.\n"
             "  transformers: enable pre-save / post-load pipeline.")
        .def("set_factory", &adapter::Repository::set_factory, py::arg("factory"),
             "Set factory callable: factory(key, data) -> entity.")
        .def("clear_factory", &adapter::Repository::clear_factory)
        .def("add_pre_transform", &adapter::Repository::add_pre_transform, py::arg("func"),
             "Add pre-save transformer func(table, data) -> data. No-op if transformers disabled.")
        .def("add_post_transform", &adapter::Repository::add_post_transform, py::arg("func"),
             "Add post-load transformer func(table, rows) -> rows. No-op if transformers disabled.")
        .def("fetch_raw",
             [](adapter::Repository &self, const adapter::QueryAdapter &query) {
                 return self.fetch_raw(query);
             },
             py::arg("query"),
             "Fetch raw data via query without transforms/factory; returns None if not found.")
        .def("get", &adapter::Repository::get, py::arg("key"),
             "Get by key (raises if not found).")
        .def("get", &adapter::Repository::get_default,
             py::arg("key"), py::arg("default"),
             "Get with default (like dict.get).")
        .def("contains", &adapter::Repository::contains, py::arg("key"))
        .def("save", &adapter::Repository::save,
             py::arg("key"), py::arg("value"))
        .def("bulk_insert", &adapter::Repository::bulk_insert,
             py::arg("table"), py::arg("columns"), py::arg("rows"),
             py::arg("batch_size") = 1000, py::arg("table_hint") = "TABLOCK")
        .def("bulk_upsert", &adapter::Repository::bulk_upsert,
             py::arg("table"), py::arg("columns"), py::arg("rows"),
             py::arg("key_column") = "id", py::arg("batch_size") = 500,
             py::arg("table_hint") = "TABLOCK")
        .def("persist_dataframe", &adapter::Repository::persist_dataframe,
             py::arg("table"), py::arg("data_frame"),
             py::arg("key_column") = "id",
             py::arg("prefer_arrow") = true,
             py::arg("table_hint") = "TABLOCK",
             py::arg("batch_size") = 1000,
             "Persist a DataFrame via Arrow BCP when available, with fallback to bulk_upsert.")
        .def("__getitem__", &adapter::Repository::get, py::arg("key"))
        .def("__setitem__", &adapter::Repository::save,
             py::arg("key"), py::arg("value"))
        .def("__repr__", &adapter::Repository::repr);

}
