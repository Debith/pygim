#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "_iterlib_fast/flatten.h"
#include "_iterlib_fast/iterutils.h"
#include <iostream>         // std::string

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(common_fast, m)
{
    m.doc() = "Python Gimmicks Common library."; // optional module docstring

    // is_container
    m.def("is_container", (bool (*)(const py::str&)) &is_container, "A function that checks if a Python str is a container.");
    m.def("is_container", (bool (*)(const py::bytes&)) &is_container, "A function that checks if a Python bytes is a container.");
    m.def("is_container", (bool (*)(const py::type&)) &is_container, "A generic function that checks if a Python type is a container.");
    m.def("is_container", (bool (*)(const py::iterable&)) &is_container, "A function that converts an iterable to a tuple.");
    m.def("is_container", (bool (*)(const py::memoryview&)) &is_container, "A function that checks if a Python memoryview is a container.");
    //m.def("is_container", (bool (*)(const py::handle&)) &is_container, "A generic function that checks if a Python object is a container.");

    // tuplify
    m.def("tuplify", (py::tuple (*)(const py::bytes&)) &tuplify, "A function that converts a bytes object to a single-element tuple");
    m.def("tuplify", (py::tuple (*)(const py::str&)) &tuplify, "A function that converts a string object to a single-element tuple.");
    m.def("tuplify", (py::tuple (*)(const py::tuple&)) &tuplify, "A function that converts a tuple to a tuple.");
    m.def("tuplify", (py::tuple (*)(const py::dict&)) &tuplify, "A function that converts a dict to a tuple of key-value pairs.");
    m.def("tuplify", (py::tuple (*)(const py::iterable&)) &tuplify, "A function that converts an iterable to a tuple.");
    m.def("tuplify", (py::tuple (*)(const py::handle&)) &tuplify, "A function that converts a generic object to a single-element tuple.");

    py::class_<FlattenGenerator>(m, "flatten")
        .def(py::init([](py::object objs) {
            std::cout << "-> init" << std::endl;
            return new FlattenGenerator(_ensure_iter(objs));
        }))
        .def("__iter__", [](const py::object &self)
             { return self; })
        .def("__next__",
            [](FlattenGenerator *self)
            {
                std::cout << "-> __next__" << std::endl;
                if (self->isComplete())
                {
                    self->iterators.clear();
                    throw py::stop_iteration();
                }

                //py::gil_scoped_release release;
                auto result = self->next();

                std::cout << "<- __next__" << py::str(result) << std::endl;
                return result;
            });

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}