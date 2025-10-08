
#include <pybind11/pybind11.h>
#include "gimdict.h"

namespace py = pybind11;

PYBIND11_MODULE(gimdict, m) {
    m.doc() = "High-performance dictionary implementation.";

    py::class_<GimDict>(m, "GimDict", R"doc(
A simple dictionary-like class with C++ backing for high performance.

Examples:
    >>> from pygim import gimdict
    >>> d = gimdict.GimDict()
    >>> d['key'] = 'value'
    >>> d['key']
    'value'
)doc")
        .def(py::init<>())
        .def("__setitem__", &GimDict::set_item, "Set an item")
        .def("__getitem__", &GimDict::get_item, "Get an item")
        .def("__contains__", &GimDict::contains, "Check if key exists")
        .def("__len__", &GimDict::size, "Get the number of items")
        .def("clear", &GimDict::clear, "Remove all items")
        .def("__repr__", &GimDict::repr);
}
