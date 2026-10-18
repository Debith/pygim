#pragma once

#include "core_utils.h"
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

namespace py = pybind11;

// Whether `instance` is a generator. CPython exposes the test directly as the PyGen_Check macro
// (Py_TYPE(op) == &PyGen_Type), which is exactly what isinstance(x, types.GeneratorType) asks — so
// no `import types` at run time and no per-call attribute lookup are needed.
inline bool is_generator(const py::object &instance) {
    return PyGen_Check(instance.ptr());
}
