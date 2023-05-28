#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <iostream>         // std::string

namespace py = pybind11;

class FlattenGenerator {
public:
    FlattenGenerator(py::iterable items) :
        mItems(items),
        cur(items.attr("__iter__")())
        {};

    bool isComplete() {
        // std::cout << "-> isComplete()" << std::endl;
        return cur == py::iterator::sentinel();
    };

    py::object next() {
        // std::cout << "-> next()" << std::endl;
        auto last = py::cast<py::object>(*cur);
        ++cur;

        // std::cout << "<- next()" << std::endl;
        return last;
    };

private:
    py::iterable mItems;
    py::iterator cur;
};
