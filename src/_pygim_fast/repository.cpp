#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "repository.h"
#include "repository/policy_query/query.h"

namespace py = pybind11;

PYBIND11_MODULE(repository, m) {
    py::class_<pygim::Query>(m, "Query")
        .def("select", &pygim::Query::select, py::arg("columns"),
             "Override the SELECT list for the query builder.")
        .def("from_table", &pygim::Query::from_table, py::arg("table"),
             "Set the table (or subquery) for the FROM clause.")
        .def("where", &pygim::Query::where, py::arg("clause"), py::arg("param"),
             "Append a WHERE clause with a bound parameter value.")
        .def("limit", &pygim::Query::limit, py::arg("rows"),
             "Apply or clear a LIMIT clause (<=0 clears the limit).")
        .def("build", &pygim::Query::build, py::return_value_policy::move,
             "Freeze the current builder state into a new Query copy.")
        .def("clone",
             [](const pygim::Query &self) { return pygim::Query(self); },
             py::return_value_policy::move,
             "Return a deep copy of this Query without rebuilding.")
        .def("sql", &pygim::Query::sql,
             "Materialize the SQL text for this query (triggers a build if needed).")
        .def("params", &pygim::Query::params,
             "Return the list of bound parameters for the query.");

    py::class_<pygim::policy_query::QueryFactory>(m, "QueryFactory")
        .def_static("make_default", &pygim::policy_query::QueryFactory::make_default,
                    "Construct a Query using the default policy (currently MSSQL).")
        .def_static("for_connection", &pygim::policy_query::QueryFactory::make_for_connection,
                    py::arg("connection"),
                    "Select an appropriate Query policy for the provided connection string.");

     py::class_<pygim::Repository>(m, "Repository", py::dynamic_attr())
        .def(py::init<bool>(), py::arg("transformers")=false,
             "Create a Repository.\n\nParameters:\n  transformers: enable transformer pipeline (pre-save & post-load).")
        .def("add_strategy", &pygim::Repository::add_strategy, py::arg("strategy"),
             "Register a strategy object implementing fetch(key)->data|None and save(key,value)->None.")
        .def("set_factory", &pygim::Repository::set_factory, py::arg("factory"),
             "Set factory callable factory(key, data)->entity.")
        .def("clear_factory", &pygim::Repository::clear_factory)
        .def("add_pre_transform", &pygim::Repository::add_pre_transform, py::arg("func"),
             "Add pre-save transformer func(key, value)->value. No-op if transformers disabled.")
        .def("add_post_transform", &pygim::Repository::add_post_transform, py::arg("func"),
             "Add post-load transformer func(key, value)->value. No-op if transformers disabled.")
        .def("fetch_raw", &pygim::Repository::fetch_raw, py::arg("key"),
             "Fetch raw data from strategies without transforms/factory; returns None if not found.")
        .def("get", &pygim::Repository::get, py::arg("key"))
        .def("get", &pygim::Repository::get_default, py::arg("key"), py::arg("default"),
             "Get with default (like dict.get).")
        .def("contains", &pygim::Repository::contains, py::arg("key"))
        .def("save", &pygim::Repository::save, py::arg("key"), py::arg("value"))
        .def("bulk_insert", &pygim::Repository::bulk_insert, py::arg("table"), py::arg("columns"), py::arg("rows"), py::arg("batch_size")=1000, py::arg("table_hint")="TABLOCK",
             "Bulk insert helper: forward to strategies supporting bulk_insert(table, columns, rows, batch_size)")
        .def("bulk_upsert", &pygim::Repository::bulk_upsert, py::arg("table"), py::arg("columns"), py::arg("rows"), py::arg("key_column")="id", py::arg("batch_size")=500, py::arg("table_hint")="TABLOCK",
             "Bulk upsert helper: forward to strategies supporting bulk_upsert(table, columns, rows, key_column, batch_size)")
        .def("__getitem__", &pygim::Repository::get, py::arg("key"))
        .def("__setitem__", &pygim::Repository::save, py::arg("key"), py::arg("value"))
        .def("strategies", &pygim::Repository::strategies)
        .def("pre_transforms", &pygim::Repository::pre_transforms)
        .def("post_transforms", &pygim::Repository::post_transforms)
        .def("__repr__", &pygim::Repository::repr);
}
