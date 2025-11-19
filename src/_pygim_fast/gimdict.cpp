
#include <pybind11/pybind11.h>
#include "gimdict.h"

namespace py = pybind11;

PYBIND11_MODULE(gimdict, m) {
    m.doc() = "High-performance dictionary implementation with multiple backend support.";

    // Module-level attributes
    m.attr("backends") = py::make_tuple("absl::flat_hash_map", "tsl::robin_map");
    m.attr("default_map") = "tsl::robin_map";

    // Main GimDict class implementing MutableMapping
    py::class_<GimDict>(m, "gimdict", R"doc(
A high-performance dictionary-like class with C++ backing.

This class implements the full MutableMapping interface and is compatible
with Python's collections.abc.MutableMapping abstract base class.

Examples:
    >>> from pygim import gimdict
    >>> d = gimdict()
    >>> d['key'] = 'value'
    >>> d['key']
    'value'
    >>> d |= dict(key1=1, key2=2)
    >>> list(d)
    ['key', 'key1', 'key2']
    >>> isinstance(d, dict)
    False
    >>> from collections.abc import MutableMapping
    >>> isinstance(d, MutableMapping)
    True
)doc")
        .def(py::init<>())
        .def("__setitem__", &GimDict::set_item, "Set an item")
        .def("__getitem__", &GimDict::get_item, "Get an item")
        .def("__delitem__", &GimDict::del_item, "Delete an item")
        .def("__contains__", &GimDict::contains, "Check if key exists")
        .def("__len__", &GimDict::size, "Get the number of items")
        .def("__iter__", &GimDict::iter, "Iterate over keys")
        .def("__repr__", &GimDict::repr)
        .def("__eq__", &GimDict::eq, "Check equality")
        .def("__ior__", &GimDict::ior, py::arg("other"), "In-place OR operator (|=)")
        .def("get", &GimDict::get, py::arg("key"), py::arg("default") = py::none(),
             "Get an item with optional default value")
        .def("pop", &GimDict::pop, py::arg("key"), py::arg("default") = py::none(),
             "Remove and return an item, with optional default")
        .def("popitem", &GimDict::popitem, "Remove and return an arbitrary (key, value) pair")
        .def("setdefault", &GimDict::setdefault, py::arg("key"), py::arg("default") = py::none(),
             "Get value if key exists, otherwise set and return default")
        .def("update", &GimDict::update, py::arg("other"), "Update from a dict")
        .def("clear", &GimDict::clear, "Remove all items")
        .def("keys", &GimDict::keys, "Return a list of all keys")
        .def("values", &GimDict::values, "Return a list of all values")
        .def("items", &GimDict::items, "Return a list of all (key, value) pairs");

    // Register with MutableMapping ABC
    py::module_ collections_abc = py::module_::import("collections.abc");
    py::object MutableMapping = collections_abc.attr("MutableMapping");
    MutableMapping.attr("register")(m.attr("gimdict"));
}
