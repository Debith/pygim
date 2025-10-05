#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "repository.h"

namespace py = pybind11;

PYBIND11_MODULE(repository, m) {
    py::class_<pygim::Repository>(m, "Repository")
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
        .def("__getitem__", &pygim::Repository::get, py::arg("key"))
        .def("__setitem__", &pygim::Repository::save, py::arg("key"), py::arg("value"))
        .def("strategies", &pygim::Repository::strategies)
        .def("pre_transforms", &pygim::Repository::pre_transforms)
        .def("post_transforms", &pygim::Repository::post_transforms)
        .def("__repr__", &pygim::Repository::repr);

}
