#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <pybind11/pybind11.h>

namespace pygim::wiring::detail {

namespace py = pybind11;

inline void ensure_callable(const py::object& obj, std::string_view label) {
    if (!PyCallable_Check(obj.ptr())) {
        throw py::type_error(std::string(label) + " must be callable");
    }
}

inline void ensure_callables(const std::vector<py::object>& objects, std::string_view label) {
    for (const auto& obj : objects) {
        ensure_callable(obj, label);
    }
}

inline bool is_instance_of(const py::object& instance, const py::object& interface) {
    return py::isinstance(instance, interface);
}

inline void ensure_instance_matches_interface(const py::object& instance, const py::object& interface) {
    if (!is_instance_of(instance, interface)) {
        throw std::runtime_error("Resolved object does not implement required interface/protocol");
    }
}

} // namespace pygim::wiring::detail