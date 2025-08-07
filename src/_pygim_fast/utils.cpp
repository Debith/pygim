
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "registry.h"

namespace py = pybind11;

PYBIND11_MODULE(utils, m) {
    m.doc() = "Utilities for Python.";
    m.def("is_dunder",
          py::overload_cast<const std::string &>(&is_dunder),
          "Check if a std::string is a Python dunder name");
    m.def("is_dunder",
          py::overload_cast<const py::str &>(&is_dunder),
          "Check if a py::str is a Python dunder name");

    m.def("to_csv",
          py::overload_cast<std::vector<std::string>, bool>(&to_csv),
          "Convert a vector of strings to a CSV string");
}
