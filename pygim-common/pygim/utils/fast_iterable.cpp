#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "flatten.h"
#include "iterutils.h"
#include <iostream>         // std::string

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(fast_iterable, m)
{
    m.doc() = "Module of fast iterables."; // optional module docstring
    m.def("tuplify", (py::tuple (*)(const py::bytes&)) &tuplify, "A function that converts a bytes object to a single-element tuple");
    m.def("tuplify", (py::tuple (*)(const py::str&)) &tuplify, "A function that converts a string object to a single-element tuple.");
    m.def("tuplify", (py::tuple (*)(const py::tuple&)) &tuplify, "A function that converts a tuple to a tuple.");
    m.def("tuplify", (py::tuple (*)(const py::dict&)) &tuplify, "A function that converts a dict to a tuple of key-value pairs.");
    m.def("tuplify", (py::tuple (*)(const py::iterable&)) &tuplify, "A function that converts an iterable to a tuple.");
    m.def("tuplify", (py::tuple (*)(const py::handle&)) &tuplify, "A function that converts a generic object to a single-element tuple.");
    m.def("flatten_simple", [](py::iterable objects) {
        py::iterator it = py::iter(objects);
        py::list results;
        for (; it != py::iterator::sentinel(); ++it) {
            if (py::isinstance<py::list>(*it)) {
                py::iterator it2 = py::iter(*it);
                for (; it2 != py::iterator::sentinel(); ++it2) {
                    results.append(*it2);
                }
            } else {
                results.append(*it);
            }
        }
        return results;
    });
    py::class_<FlattenGenerator>(m, "flatten")
        .def(py::init([](py::object objs) { return new FlattenGenerator(_ensure_iter(objs)); }))
        .def("__iter__", [](const py::object &self)
             { return self; })
        .def("__next__",
             [](FlattenGenerator *self)
             {
                // std::cout << "-> __next__" << std::endl;
                 if (self->isComplete())
                 {
                    // std::cout << "<- __next__ (complete)" << std::endl;
                     throw py::stop_iteration();
                 }

                 //py::gil_scoped_release release;
                 auto result = self->next();

                 // std::cout << "<- next" << std::endl;
                 return result;
             });

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}