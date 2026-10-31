// persistence/adapter/query_bindings.h
// The Python face of the query builder and its dialects: plain values in, SQL text out.
//
// Example:
//   q = Query().select("id").from_table("dbo.users").where("id > 5").limit(10)
//   MssqlDialect().render(q)                 -> "SELECT TOP 10 [id] FROM [dbo].[users] WHERE id > 5"
//   MssqlDialect().render(Query("SELECT 1")) -> "SELECT 1"        (raw SQL passes through)
//   store.load(q)                            -> the same SQL, run on the store's connection
//
// Nothing here reaches a connection, a store or a session: a query is built and rendered from plain
// values, and only DataStore.load runs one. test_layering.py holds the core headers bound here to
// the standard library, Query and logging.

#pragma once

#include "../core/dialect.h"
#include "../core/query.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <string_view>

namespace pygim::adapter {

namespace py = pybind11;

/// Binds `Query`, the fluent builder: each step stores intent on the same object and returns it,
/// one string or vector append per step. On the example: columns ["id"], table "dbo.users",
/// where_clause "id > 5", limit_value 10, is_raw() False.
inline void bind_query(py::module_& m) {
    using core::Query;
    py::class_<Query>(m, "Query",
                      "A query as intent — columns, table, where clause, limit — or as raw SQL.\n"
                      "Built from plain values; a dialect renders it and DataStore.load runs it.")
        .def(py::init<>())
        .def(py::init<std::string_view>(), py::arg("raw_sql"),
             "Raw SQL, passed through unchanged by every dialect.")
        .def("select", &Query::select, py::arg("col"), py::return_value_policy::reference_internal,
             "Add a column; returns this query.")
        .def("from_table", &Query::from_table, py::arg("table"), py::return_value_policy::reference_internal,
             "Set the table, optionally schema-qualified ('dbo.users'); returns this query.")
        .def("where", &Query::where, py::arg("clause"), py::return_value_policy::reference_internal,
             "Set the WHERE clause, as SQL text; returns this query.")
        .def("limit", &Query::limit, py::arg("n"), py::return_value_policy::reference_internal,
             "Keep at most n rows; returns this query.")
        .def("is_raw", &Query::is_raw, "True when the query was given as raw SQL.")
        .def_property_readonly("table", [](Query const& q) { return std::string(q.table()); })
        .def_property_readonly("columns", &Query::columns)
        .def_property_readonly("where_clause", [](Query const& q) { return std::string(q.where_clause()); })
        .def_property_readonly("limit_value", [](Query const& q) -> py::object {
            if (q.limit_value()) return py::int_(*q.limit_value());
            return py::none();
        })
        .def_property_readonly("raw_sql", [](Query const& q) { return std::string(q.raw_sql()); });
}

/// Binds dialect `D` under `name`. `render(query)` is core::build_sql — the function load uses — so
/// raw SQL passes through and intent is rendered by the dialect; one string built per call, sized
/// up front. On the example, MssqlDialect renders the first line above.
template <core::DialectPolicy D>
void bind_dialect(py::module_& m, const char* name, const char* doc) {
    py::class_<D>(m, name, doc)
        .def(py::init<>())
        .def("render", [](D const& d, core::Query const& q) { return core::build_sql(q, d); }, py::arg("query"),
             "The SQL this dialect runs for the query; raw SQL unchanged.")
        .def("quote_identifier", &D::quote_identifier, py::arg("name"),
             "One identifier quoted for this dialect.");
}

} // namespace pygim::adapter
