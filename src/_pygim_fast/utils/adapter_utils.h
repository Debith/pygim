#pragma once

#include "core_utils.h"
#include "python_names.h"
#include <algorithm>
#include <cstddef>
#include <string_view>
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

namespace py = pybind11;

// Whether `instance` is a generator, as isinstance(x, types.GeneratorType) says — Python's own
// answer, which inspect.isgenerator gives too. types.GeneratorType is PyGen_Type, so the C API asks
// it directly: no `import types` at run time. Not PyGen_Check: that reads the type alone, where
// isinstance also believes an object's __class__.
inline bool is_generator(const py::object &instance) {
    const int answer = PyObject_IsInstance(instance.ptr(), reinterpret_cast<PyObject*>(&PyGen_Type));
    if (answer < 0) throw py::error_already_set();
    return answer == 1;
}

namespace pygim::adapter {

/// A string literal usable as a template argument, so a name can select a function: interned<"co_names">().
/// C++ requires a class used as a template argument to have public data, so this one is a struct.
template <std::size_t N>
struct literal {
    char text[N];
    constexpr literal(const char (&s)[N]) { std::copy_n(s, N, text); }
};
static_assert(std::string_view{literal{"co_names"}.text} == "co_names");

/// `Text` as an interned str: made on first use, inside a call and so under the GIL — never while the
/// extension loads, when Windows holds no GIL (project #254) — and kept for the life of the process.
/// One function, and so one str, per spelling; a failed first use throws and the next retries.
/// Example: interned<"co_names">() is the str 'co_names', the same object on every call.
template <literal Text>
PyObject* interned() {
    static PyObject* const name = [] {
        PyObject* made = PyUnicode_InternFromString(Text.text);
        if (made == nullptr) throw py::error_already_set();
        return made;
    }();
    return name;
}

// getattr(object, name) asked once, as hasattr asks: the attribute, or a null object when it has
// none (an AttributeError, dropped); any other error raises. A miss makes no exception where Python
// lets it (3.12 on, for classes too); pybind11's py::hasattr (PyObject_HasAttrString) makes the
// AttributeError, its message formatted, then drops it — half a microsecond a miss — and drops any
// other error with it.
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

// hasattr(object, name) as Python asks it: an AttributeError is "no", any other error raises.
inline bool has_attribute(const py::handle& object, const py::handle& name) {
    return static_cast<bool>(optional_attribute(object, name));
}

/// A Python class's names as a NamePolicy: its __name__ (what mangling uses) and __qualname__ (what its
/// functions' qualified names start with), each read as str() of the attribute.
/// Example: name_policy_of(Outer.Inner).mangled("__x") is "_Inner__x".
inline python_names::NamePolicy name_policy_of(const py::handle& cls) {
    auto text = [&](PyObject* attribute) {
        PyObject* value = PyObject_GetAttr(cls.ptr(), attribute);
        if (value == nullptr) throw py::error_already_set();
        return py::str(py::reinterpret_steal<py::object>(value)).cast<std::string>();
    };
    return python_names::NamePolicy{text(interned<"__name__">()), text(interned<"__qualname__">())};
}

}  // namespace pygim::adapter
