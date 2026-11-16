#pragma once

#include "core_utils.h"
#include <algorithm>
#include <cstddef>
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

namespace pygim::adapter {

/// A string literal as a template argument: interned<"__init__">().
template <std::size_t N>
struct name_literal {
    char text[N]{};
    constexpr name_literal(const char (&given)[N]) { std::copy_n(given, N, text); }
};
static_assert(name_literal("__init__").text[2] == 'i');   // the name interned<"__init__">() makes

/// The interned str of `Name`, made at its first use and kept for the process's life, as CPython
/// keeps interned strings. A lookup by it hits a type's attribute cache, which is keyed by the name
/// object; the fresh str that py::object::attr("...") and PyObject_GetAttrString make for each lookup
/// misses it, 70 ns a lookup more. Made in a function-local static, never at static initialization:
/// Windows loads an extension with the GIL released.
template <name_literal Name>
[[nodiscard]] inline py::handle interned() {
    static PyObject* const made = [] {
        PyObject* name = PyUnicode_InternFromString(Name.text);
        if (name == nullptr) throw py::error_already_set();   // not kept: the next use tries again
        return name;
    }();
    return made;
}

/// getattr(object, name) asked once, as hasattr asks: the attribute, or a null object when it has
/// none (an AttributeError, dropped); any other error raises. A miss makes no exception where Python
/// lets it (3.12 on, for classes too); pybind11's py::hasattr (PyObject_HasAttrString) makes the
/// AttributeError, its message formatted, then drops it (half a microsecond a miss) and drops any
/// other error with it.
inline py::object optional_attribute(const py::handle& object, const py::handle& name) {
    PyObject* found = nullptr;
#if PY_VERSION_HEX >= 0x030D0000
    const int got = PyObject_GetOptionalAttr(object.ptr(), name.ptr(), &found);
#else
    const int got = _PyObject_LookupAttr(object.ptr(), name.ptr(), &found);
#endif
    if (got < 0) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(found);
}

/// hasattr(object, name) as Python asks it: an AttributeError is "no", any other error raises.
inline bool has_attribute(const py::handle& object, const py::handle& name) {
    return static_cast<bool>(optional_attribute(object, name));
}

}  // namespace pygim::adapter
