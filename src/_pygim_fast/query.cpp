#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "query.h"  // shim now includes repository/generic/query.h

namespace py = pybind11;

PYBIND11_MODULE(query, m) {
    py::class_<pygim::Query>(m, "Query")
        .def(py::init<std::string, std::vector<py::object>>(), py::arg("sql"), py::arg("params")=std::vector<py::object>{},
             "Create a Query manually. Normally built via QueryBuilder.")
        .def_property_readonly("sql", &pygim::Query::sql)
        .def("params", &pygim::Query::params)
        .def("__repr__", &pygim::Query::repr);

    py::class_<pygim::QueryBuilder>(m, "QueryBuilder")
        .def(py::init<>())
        .def("select", &pygim::QueryBuilder::select, py::arg("columns"))
        .def("from_table", &pygim::QueryBuilder::from, py::arg("table"))
        .def("where", &pygim::QueryBuilder::where, py::arg("clause"), py::arg("param"))
        .def("limit", &pygim::QueryBuilder::limit, py::arg("n"))
        .def("build", &pygim::QueryBuilder::build);
}
