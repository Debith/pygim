#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <iostream>         // std::string

#include "flatten.h"
#include "iterutils.h"

FlattenGenerator::FlattenGenerator() {}

FlattenGenerator::~FlattenGenerator() {
    //iterators.clear();
}

FlattenGenerator::FlattenGenerator(py::iterator items) {
    iterators.push_back(items);
}


FlattenGenerator::FlattenGenerator(const FlattenGenerator& other) :
    iterators(other.iterators) {
}


bool FlattenGenerator::isComplete() {
    while (!iterators.empty() && iterators.back() == py::iterator::sentinel()) {
        iterators.pop_back();
    }
    return iterators.empty();
}

py::handle FlattenGenerator::next() {
    std::cout << "-> next()" << std::endl;
    py::iterator &it = iterators.back();
    auto last = *it;
    ++it;

    if (is_container(last)) {
        std::cout << "it's a list!" << py::str(last) << std::endl;
        iterators.push_back(py::iter(last));
        if (!isComplete()) {
            return next();
        } else {
            throw py::stop_iteration();
        }
    } else {
        std::cout << "<- next() " << py::str(last) << std::endl;
        return last;
    }
}
