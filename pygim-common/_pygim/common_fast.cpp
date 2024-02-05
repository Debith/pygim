#define PYBIND11_DETAILED_ERROR_MESSAGES

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "_iterlib_fast/iterutils.h"
#include "_gimmicks_fast/random.h"
#include "_gimmicks_fast/id.h"
#include <iostream>         // std::string

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

template<typename T>
void bindID(py::module_ &m, const char* className) {
    using IDType = ID<T>;
    py::class_<IDType>(m, className, "Unique ID class.")
        .def(py::init<T>(), "Constructor with ID value")
        .def_static("random",
                    &ID<T>::random,
                    "Static method to generate a pseudo random ID."
                    )
        .def("__hash__", &IDType::hash, "Get hash value of the ID")
        .def("__eq__", [](const IDType& a, const IDType& b) { return a == b; }, "Equality comparison")
        .def("__repr__", [className](const IDType& id) {
            return "<" + std::string(className) + ":" + std::to_string(id.hash()) + ">";
        }, "String representation");
}

PYBIND11_MODULE(common_fast, m)
{
    m.doc() = "Python Gimmicks Common library."; // optional module docstring

    // is_container
    m.def("is_container", (bool (*)(const py::int_&))       &is_container, "A function that checks if a Python int_ is a container.");
    m.def("is_container", (bool (*)(const py::float_&))     &is_container, "A function that checks if a Python float_ is a container.");
    m.def("is_container", (bool (*)(const py::str&))        &is_container, "A function that checks if a Python str is a container.");
    m.def("is_container", (bool (*)(const py::bytearray&))  &is_container, "A function that checks if a Python bytearray is a container.");
    m.def("is_container", (bool (*)(const py::bytes&))      &is_container, "A function that checks if a Python bytes is a container.");
    m.def("is_container", (bool (*)(const py::tuple&))      &is_container, "A function that checks if a Python tuple is a container.");
    m.def("is_container", (bool (*)(const py::list&))       &is_container, "A function that checks if a Python list is a container.");
    m.def("is_container", (bool (*)(const py::set&))        &is_container, "A function that checks if a Python set is a container.");
    m.def("is_container", (bool (*)(const py::dict&))       &is_container, "A function that checks if a Python dict is a container.");
    m.def("is_container", (bool (*)(const py::type&))       &is_container, "A generic function that checks if a Python type is a container.");
    m.def("is_container", (bool (*)(const py::memoryview&)) &is_container, "A function that checks if a Python memoryview is a container.");
    m.def("is_container", (bool (*)(const py::iterator&))   &is_container, "A function that checks if a Python iterator is a container.");
    m.def("is_container", (bool (*)(const py::iterable&))   &is_container, "A function that checks if a Python iterable is a container.");
    m.def("is_container", (bool (*)(const py::handle&))     &is_container, "A generic function that checks if a Python object is a container.");

    // tuplify
    m.def("tuplify", (py::tuple (*)(const py::bytes&))      &tuplify, "A function that converts a bytes object to a single-element tuple");
    m.def("tuplify", (py::tuple (*)(const py::str&))        &tuplify, "A function that converts a string object to a single-element tuple.");
    m.def("tuplify", (py::tuple (*)(const py::tuple&))      &tuplify, "A function that converts a tuple to a tuple.");
    m.def("tuplify", (py::tuple (*)(const py::dict&))       &tuplify, "A function that converts a dict to a tuple of key-value pairs.");
    m.def("tuplify", (py::tuple (*)(const py::iterable&))   &tuplify, "A function that converts an iterable to a tuple.");
    m.def("tuplify", (py::tuple (*)(const py::handle&))     &tuplify, "A function that converts a generic object to a single-element tuple.");

    m.def("rand_integer", &Random::randomInteger64, "Generate a random 64-bit number");
    m.def("randint", &Random::randomInteger, "Generate a random number within a given range inclusive", py::arg("min"), py::arg("max"));
    m.def("randints", &Random::randomIntegers, "Generate a list of random 64-bit numbers", py::arg("count") = 1);

    // Class ID
    bindID<uint64_t>(m, "ID");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}