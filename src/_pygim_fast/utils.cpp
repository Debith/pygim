
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "utils/utils.h"

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

    m.def("format_bytes_per_second",
          &format_bytes_per_second,
          py::arg("bytes_per_second"),
          py::arg("precision") = 2,
          "Format a bytes-per-second value using human-readable units with configurable precision.");

    m.def("calculate_rate",
          &calculate_rate,
          py::arg("quantity"),
          py::arg("quantity_unit"),
          py::arg("duration"),
          py::arg("duration_unit"),
          py::arg("precision") = 2,
          "Compute a throughput string from quantity+unit over duration+unit.");
}
