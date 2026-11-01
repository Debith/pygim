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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
        .def(py::init([](std::optional<std::string_view> raw_sql, const std::vector<std::string>& columns, std::string_view table,
                         std::string_view where, std::optional<int> limit) {
                 if (raw_sql) {
                     if (!columns.empty() || !table.empty() || !where.empty() || limit)
                         throw py::value_error("Query takes raw SQL or its parts, not both: raw SQL is passed through whole");
                     return Query(*raw_sql);
                 }
                 Query q;
                 for (const std::string& column : columns) q.select(column);
                 if (!table.empty()) q.from_table(table);
                 if (!where.empty()) q.where(where);
                 if (limit) q.limit(*limit);
                 return q;
             }), py::arg("raw_sql") = py::none(), py::kw_only(), py::arg("columns") = std::vector<std::string>{},
             py::arg("table") = "", py::arg("where") = "", py::arg("limit") = py::none(),
             "Raw SQL, passed through unchanged by every dialect; or the parts the builder's steps set,\n"
             "so Query(columns=['id'], table='t', limit=5) == Query().select('id').from_table('t').limit(5).")
        .def("__eq__", [](Query const& a, Query const& b) { return a == b; }, py::is_operator(), py::arg("other"),
             "The same intent, or the same raw SQL.")   // NotImplemented for any other type
        .def("__repr__", [](Query const& q) {
                 const auto shown = [](std::string_view text) { return py::repr(py::str(std::string(text))).cast<std::string>(); };
                 if (q.is_raw()) return "Query(" + shown(q.raw_sql()) + ")";
                 std::string parts;
                 const auto add = [&parts](const std::string& part) { parts += (parts.empty() ? "" : ", ") + part; };
                 if (!q.columns().empty()) add("columns=" + py::repr(py::cast(q.columns())).cast<std::string>());
                 if (!q.table().empty()) add("table=" + shown(q.table()));
                 if (!q.where_clause().empty()) add("where=" + shown(q.where_clause()));
                 if (q.limit_value()) add("limit=" + std::to_string(*q.limit_value()));
                 return "Query(" + parts + ")";
             })
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
