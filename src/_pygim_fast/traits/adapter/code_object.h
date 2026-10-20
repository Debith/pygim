// traits/adapter/code_object.h — a code object's fields through the C API: the names they are read and
// replaced by, and the code.replace() call that derives a new code object (code objects never change).
//
// Worked example, for `def f(self): return self.__secret` written in class Trait:
//
//   field(code, co_names)                                -> ('_Trait__secret',)  what the bytecode looks up
//   field(code, co_consts)                               -> (None,)              where nested code would be
//   field(code, co_qualname)                             -> 'Trait.f'            Python 3.11 and later
//   ReplaceCall::execute(code, names, constants, qualname) -> a new code object with those three fields
//
// The names are interned once, when the extension is imported: the interpreter is running and the
// import holds the GIL. Interning a short ASCII name fails only when memory is exhausted; the error
// is then left set and the import fails with it. Every call holds the GIL, as every pybind11 call does.

#pragma once

#include <pybind11/pybind11.h>

namespace pygim::traits::code_object {

namespace py = pybind11;

/// `text` as an interned str, kept for the life of the process; nullptr, with the error set, when it
/// cannot be made.
inline PyObject* interned(const char* text) noexcept { return PyUnicode_InternFromString(text); }

/// The attribute names a code object is read and replaced by.
inline PyObject* const co_names = interned("co_names");        // what the bytecode looks up, by index
inline PyObject* const co_consts = interned("co_consts");      // the constants: nested code lives here
inline PyObject* const co_qualname = interned("co_qualname");  // 'Trait.f'
/// Whether a code object has co_qualname: Python 3.11 and later. A compile-time fact, because an
/// extension is built for one Python version.
inline constexpr bool has_qualname = PY_VERSION_HEX >= 0x030B0000;

/// The field `name` of `code`. Example: field(code, co_names) is its co_names tuple.
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
        PyObject* result = PyObject_VectorcallMethod(replace, arguments, 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                                     keywords);
        if (result == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(result);
    }

private:
    static inline PyObject* const replace = interned("replace");
    static inline PyObject* const keywords = has_qualname ? PyTuple_Pack(3, co_names, co_consts, co_qualname)
                                                          : PyTuple_Pack(2, co_names, co_consts);
};

}  // namespace pygim::traits::code_object
