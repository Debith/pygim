#pragma once

#include "core_utils.h"
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

namespace py = pybind11;

inline bool is_dunder(const py::str &attr) {
    std::string s = static_cast<std::string>(attr);
    return is_dunder(s);
}

inline bool is_generator(const py::object &instance) {
    static py::object generator_type = py::module::import("types").attr("GeneratorType");
    return py::isinstance(instance, generator_type);
}
