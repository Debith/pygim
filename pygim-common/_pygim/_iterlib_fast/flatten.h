#ifndef FLATTEN_GENERATOR_H
#define FLATTEN_GENERATOR_H

#include <vector>
#include <pybind11/pybind11.h>

#include "iterutils.h"

namespace py = pybind11;

inline py::iterator _ensure_iter(py::handle obj) {
    if (py::isinstance<py::iterator>(obj)) {
        return obj.cast<py::iterator>();
    }

    if (!is_container(obj)) {
        return py::iter(tuplify(obj));
    }

    return py::iter(obj);
};


class FlattenGenerator {
public:
    FlattenGenerator();
    FlattenGenerator(py::iterator items);
    ~FlattenGenerator();

    bool isComplete();

    py::handle next();

private:
    std::vector<py::iterator> iterators;
};

#endif // FLATTEN_GENERATOR_H
