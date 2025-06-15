#define PYBIND11_DETAILED_ERROR_MESSAGES

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>

#include "pathset.h"
#include <iostream>         // std::string

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(pathset, m)
{
    m.doc() = "Python Gimmicks Common library."; // optional module docstring

   // PathSet
    m.def("match_pattern", &match_pattern, py::arg("pattern"), py::arg("string"),
          "Match a string against a glob pattern");

    py::class_<PathSet>(m, "PathSet")
        .def(py::init<>())
        .def(py::init<const fs::path&>())
        .def(py::init<const std::string&>())
        .def(py::init<const std::vector<fs::path>&>())
        .def(py::init<const std::initializer_list<fs::path>&>())
        .def_static("cwd", &PathSet::cwd)
        .def("__len__",         &PathSet::size)
        .def("__bool__",        [](const PathSet &ps) { return !ps.empty(); })
        .def("__repr__",        &PathSet::repr)
        .def("__str__",         &PathSet::str)
        .def("__contains__",    &PathSet::contains)
        // Iterator over PathSet using internal m_paths
        .def("__iter__", [](const PathSet &ps) {
             return py::make_iterator(ps.m_paths.begin(), ps.m_paths.end(), py::return_value_policy::reference_internal);
         }, py::keep_alive<0,1>())
        // heterogeneous operators: PathSet  &/|  Filter  → Query
        .def("__and__",
             [](const PathSet& s, const Filter& f) { return s & f; },
             py::is_operator())
        .def("__or__",
             [](const PathSet& s, const Filter& f) { return s | f; },
             py::is_operator())
        .def("__add__", &PathSet::operator+)
        // removal operators: PathSet -= PathSet and PathSet -= str
        .def(py::self -= py::self)
        .def(py::self -= py::str())
        .def("__eq__",          &PathSet::operator==)
        .def("clone", &PathSet::clone)
        .def("read_all_files", &PathSet::read_all_files);


    /* ----------------  Filter  ----------------- */
    py::class_<Filter>(m, "Filter")
        // Boolean algebra between filters
        .def(py::self & py::self)                   //  f1 & f2
        .def(py::self | py::self)                   //  f1 | f2
        .def("__invert__",                          //  ~f   (maps to !f in C++)
             [](const Filter& f) { return !f; },
             py::is_operator());

    /* ----------------  Query<PathSet> ---------- */
    py::class_<QueryPS>(m, "Query")
        .def("__and__", [](const QueryPS& q, const Filter& f) { return q & f; }, py::is_operator())
        .def("__or__",  [](const QueryPS& q, const Filter& f) { return q | f; }, py::is_operator())
        .def("eval", &QueryPS::eval, "Materialise the filtered paths as a new PathSet")
        // iterating in Python triggers a lazy eval under the hood
        .def("__iter__", [](const QueryPS& q) {
             auto ps = std::make_shared<PathSet>(q.eval());
             return py::make_iterator(ps->m_paths.begin(), ps->m_paths.end(), py::return_value_policy::reference_internal);
         }, py::keep_alive<0,1>());

    /* -------------  helper factory functions ------------- */
    m.def("ext", &ext, "Return a Filter matching a file extension");

    /* ...add size_gt(), newer_than(), etc. the same way… */

    #ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
    #else
    m.attr("__version__") = "dev";
    #endif
}