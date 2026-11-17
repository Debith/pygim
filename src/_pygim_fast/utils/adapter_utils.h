#pragma once

#include "core_utils.h"
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

namespace py = pybind11;

inline bool is_dunder(const py::str &attr) {
    std::string s = static_cast<std::string>(attr);
    return is_dunder(s);
}

// Whether `instance` is a generator, as isinstance(x, types.GeneratorType) says — Python's own
// answer, which inspect.isgenerator gives too. types.GeneratorType is PyGen_Type, so the C API asks
// it directly: no `import types` at run time. Not PyGen_Check: that reads the type alone, where
// isinstance also believes an object's __class__.
inline bool is_generator(const py::object &instance) {
    const int answer = PyObject_IsInstance(instance.ptr(), reinterpret_cast<PyObject*>(&PyGen_Type));
    if (answer < 0) throw py::error_already_set();
    return answer == 1;
}
