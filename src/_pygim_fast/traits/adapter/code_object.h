// traits/adapter/code_object.h — a code object's fields through the C API: the names they are read and
// replaced by, and the code.replace() call that derives a new code object (code objects never change).
//
// Worked example, for `def f(self): return self.__secret` written in class Trait:
//
//   field(code, co_names())                              -> ('_Trait__secret',)  what the bytecode looks up
//   field(code, co_consts())                             -> (None,)              where nested code would be
//   field(code, co_qualname())                           -> 'Trait.f'            Python 3.11 and later
//   ReplaceCall::execute(code, names, constants, qualname) -> a new code object with those three fields
//
// Each name is interned once, on first use, and kept for the life of the process. First use is inside
// a call, which holds the GIL; never while the extension loads: on Windows CPython loads an extension
// with the GIL released (Python/dynload_win.c), so no C API call may run in a static initializer —
// tests/unittests/test_no_python_at_static_init.py scans for one.

#pragma once

#include <pybind11/pybind11.h>

namespace pygim::traits::code_object {

namespace py = pybind11;

/// `text` as an interned str, kept for the life of the process.
inline PyObject* interned(const char* text) {
    PyObject* name = PyUnicode_InternFromString(text);
    if (name == nullptr) throw py::error_already_set();
    return name;
}

// The attribute names a code object is read and replaced by. A static whose initializer throws is
// not initialized, so a failed first use is retried by the next.
/// co_names: what the bytecode looks up, by index.
inline PyObject* co_names() {
    static PyObject* const name = interned("co_names");
    return name;
}
/// co_consts: the constants, where nested code lives.
inline PyObject* co_consts() {
    static PyObject* const name = interned("co_consts");
    return name;
}
/// co_qualname: 'Trait.f'.
inline PyObject* co_qualname() {
    static PyObject* const name = interned("co_qualname");
    return name;
}
/// Whether a code object has co_qualname: Python 3.11 and later. A compile-time fact, because an
/// extension is built for one Python version.
inline constexpr bool has_qualname = PY_VERSION_HEX >= 0x030B0000;

/// The field `name` of `code`. Example: field(code, co_names()) is its co_names tuple.
inline py::object field(const py::handle& code, PyObject* name) {
    PyObject* value = PyObject_GetAttr(code.ptr(), name);
    if (value == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(value);
}

/// code.replace(co_names=names, co_consts=constants, co_qualname=qualname): Python's own way to derive
/// a code object, called by vectorcall, so no kwargs dict and no str is made per call. Before Python
/// 3.11 a code object has no co_qualname, and `qualname` is not passed.
/// Example: ReplaceCall::execute(code, new_names, new_constants, new_qualname) is the new code object.
class ReplaceCall {
public:
    [[nodiscard]] static py::object execute(const py::handle& code, const py::object& names,
                                            const py::object& constants, const py::object& qualname) {
        PyObject* arguments[] = {code.ptr(), names.ptr(), constants.ptr(), qualname.ptr()};
        PyObject* result = PyObject_VectorcallMethod(method(), arguments, 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                                     keywords());
        if (result == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(result);
    }

private:
    static PyObject* method() {
        static PyObject* const name = interned("replace");
        return name;
    }
    static PyObject* keywords() {
        static PyObject* const names = [] {
            PyObject* tuple = has_qualname ? PyTuple_Pack(3, co_names(), co_consts(), co_qualname())
                                           : PyTuple_Pack(2, co_names(), co_consts());
            if (tuple == nullptr) throw py::error_already_set();
            return tuple;
        }();
        return names;
    }
};

}  // namespace pygim::traits::code_object
