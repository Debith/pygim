#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <pybind11/pybind11.h>
#include "../../errors/adapter.h"

namespace pygim::wiring::detail {

namespace py = pybind11;
namespace errors = pygim::errors;

inline void ensure_callable(const py::object& obj, std::string_view label) {
    if (!PyCallable_Check(obj.ptr())) {
        errors::raise(&errors::table::unrecognized_type_error, obj, "a callable", std::string(label));
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

/// UnrecognizedTypeError(instance, interface, `what`) when `instance` is not an `interface`.
inline void ensure_instance_matches_interface(const py::object& instance, const py::object& interface,
                                              std::string_view what = "resolved object") {
    if (!is_instance_of(instance, interface)) {
        errors::raise(&errors::table::unrecognized_type_error, instance, interface, std::string(what));
    }
}

} // namespace pygim::wiring::detail