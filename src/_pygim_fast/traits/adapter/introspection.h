// traits/adapter/introspection.h — what compose() reads from Python objects: text, a type's name, a class's
// own namespace, where code was defined and called from, an attribute's name.

#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>

namespace pygim::traits {

namespace py = pybind11;

namespace detail {

inline std::string text(const py::handle& value) { return py::str(value).cast<std::string>(); }
inline std::string type_name(const py::handle& value) { return text(py::type::of(value).attr("__name__")); }

/// A class's own namespace, through the C API: the dict itself, so no metaclass attribute named
/// `__dict__` and no descriptor is asked. compose() reads every class namespace this way — the
/// conflict check, the members of a trait, the record — so a metaclass cannot show it one thing and
/// the writes another. Example: own_namespace(Person)["greet"] is Person.greet's function.
inline py::dict own_namespace(const py::handle& cls) {
#if PY_VERSION_HEX >= 0x030C0000
    PyObject* dict = PyType_GetDict(reinterpret_cast<PyTypeObject*>(cls.ptr()));  // new reference
    return dict != nullptr ? py::reinterpret_steal<py::dict>(dict) : py::dict();
#else
    PyObject* dict = reinterpret_cast<PyTypeObject*>(cls.ptr())->tp_dict;           // borrowed
    return dict != nullptr ? py::reinterpret_borrow<py::dict>(dict) : py::dict();
#endif
}

/// "file:line" of a code object; "" for None.
inline std::string where(const py::handle& code) {
    if (code.is_none()) return {};
    return std::format("{}:{}", text(code.attr("co_filename")), text(code.attr("co_firstlineno")));
}

/// "file:line" of the Python code that called into C++: the innermost Python frame, which a pybind11
/// call does not add to — what sys._getframe(0) answers, read through the C API. "" with no Python
/// frame running.
inline std::string caller() {
    PyFrameObject* frame = PyEval_GetFrame();  // borrowed
    if (frame == nullptr) return {};
    const auto code = py::reinterpret_steal<py::object>(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
    return std::format("{}:{}", text(code.attr("co_filename")), PyFrame_GetLineNumber(frame));
}

/// The attribute name a dict key spells, by its real characters, and that name as a fresh str. The
/// characters come straight from the key, so a str subclass cannot change them through __str__ and
/// an embedded NUL is kept whole; the fresh str is what compose() writes, so the subclass instance
/// is not stored as the attribute name. A key that is not a str is refused: a name must be one.
inline std::pair<std::string, py::object> attribute_name(const py::handle& key) {
    if (!PyUnicode_Check(key.ptr()))
        throw py::type_error(std::format("a trait member's name must be a str, not {}", type_name(key)));
    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(key.ptr(), &size);  // raises on a lone surrogate (not UTF-8)
    if (data == nullptr) throw py::error_already_set();
    py::object canonical = py::reinterpret_steal<py::object>(PyUnicode_FromStringAndSize(data, size));
    if (!canonical) throw py::error_already_set();
    return {std::string(data, static_cast<std::size_t>(size)), std::move(canonical)};
}

}  // namespace detail

}  // namespace pygim::traits
