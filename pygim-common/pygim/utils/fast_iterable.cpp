#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "flatten.h"
#include <iostream>         // std::string

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(fast_iterable, m)
{
    m.doc() = "Module of fast iterables."; // optional module docstring

    py::class_<FlattenGenerator>(m, "flatten")
        .def(py::init(
            [](py::iterable objs)
            {
                return new FlattenGenerator(objs);
            }))
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