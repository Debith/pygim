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
        .def("__add__",         &PathSet::operator+)
        .def(py::self -= py::str())    // operator-=(string)
        .def(py::self -= py::self)     // operator-=(PathSet)
        .def("__eq__",          &PathSet::operator==)
        .def("__iter__", [](const PathSet &ps) {
            return py::make_iterator(ps.m_paths.begin(), ps.m_paths.end());
        }, py::keep_alive<0, 1>())
        .def("clone", &PathSet::clone)
        .def("filter_by_extension", &PathSet::filter_by_extension, py::arg("extension"))
        .def("filter_by_extensions", &PathSet::filter_by_extensions, py::arg("extensions"))
        .def("filter_existing", &PathSet::filter_existing)
        .def("read_all_files", &PathSet::read_all_files);

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}