#include <pybind11/pybind11.h>
#include "flatten.h"


int flatten(std::vector<Py>) {
    return py::make_iterator(v.begin(), v.end());
}



PYBIND11_MODULE(example, m) {
    m.doc() = "Module of fast iterables."; // optional module docstring

    m.def("flatten", [](py::list &v) {
        auto *gen = new FlattenGenerator()
        return flatten(v)
    }, py::keep_alive<0, 1>(), "Function iterates every non iterable element as a 1-dimensional array.") /* Keep vector alive while iterator is used */
}