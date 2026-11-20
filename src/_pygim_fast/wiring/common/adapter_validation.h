#pragma once

#include <optional>
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

/// A key's name as Python gives it to a keyed store: None for no name, or a str;
/// UnrecognizedTypeError for anything else.
inline std::optional<std::string> key_name(const py::object& name) {
    if (name.is_none()) return std::nullopt;
    if (!PyUnicode_Check(name.ptr())) {
        errors::raise(&errors::table::unrecognized_type_error, name,
                      py::make_tuple(py::handle(reinterpret_cast<PyObject*>(&PyUnicode_Type)), py::none()), "name");
    }
    return name.cast<std::string>();
}

/// A key as Python gives it to a keyed store: a thing alone, or the tuple (thing, name or None).
class python_key {
public:
    /// `key` split into its thing and its name; ArgumentError, "`store` key tuple must be `shape`",
    /// for a tuple of any other length.
    python_key(const py::object& key, std::string_view store, std::string_view shape) : m_thing(key), m_name(py::none()) {
        if (!PyTuple_Check(key.ptr())) return;
        if (PyTuple_GET_SIZE(key.ptr()) != 2)
            errors::raise(&errors::table::argument_error, std::string(store) + " key tuple must be " + std::string(shape));
        m_thing = py::reinterpret_borrow<py::object>(PyTuple_GET_ITEM(key.ptr(), 0));
        m_name = py::reinterpret_borrow<py::object>(PyTuple_GET_ITEM(key.ptr(), 1));
    }

    [[nodiscard]] const py::object& thing() const noexcept { return m_thing; }
    /// The name, checked by key_name().
    [[nodiscard]] std::optional<std::string> name() const { return key_name(m_name); }

private:
    py::object m_thing;
    py::object m_name;
};

} // namespace pygim::wiring::detail