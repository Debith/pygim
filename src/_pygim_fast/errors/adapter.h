#pragma once
// errors/adapter.h — how a pygim extension raises pygim's exceptions: it takes pygim.errors'
// table once, as it loads, and raises a class from it by name, with the arguments Python would
// call the class with. pygim.errors imports nothing of pygim, so taking its table from any
// module's load closes no import loop; no extension imports pygim's Python.
//
// ADAPTER layer.
//
//     PYBIND11_MODULE(utils, m) {
//         pygim::errors::load();
//         ...
//     pygim::errors::raise(&pygim::errors::table::option_error, value, options);
//     // GimOptionError: Given input ``x`` not among: sum, max, min, replace

#include "table.h"

#include <pybind11/pybind11.h>

#include <string>
#include <utility>

namespace pygim::errors {

namespace detail {

/// This module's copy of the table's address, set by load() and read by every raise after it.
inline const table* loaded = nullptr;

}  // namespace detail

/// Takes pygim.errors' table, once, as the module loads, never while running. Raises ImportError
/// when pygim.errors is missing or older than this module. The module is imported first:
/// PyCapsule_Import imports only the top package, pygim, and looks the rest up as attributes, which
/// a submodule is only once it has been imported.
inline void load() {
    PyObject* module = PyImport_ImportModule(module_name);
    if (module == nullptr) throw pybind11::error_already_set();
    Py_DECREF(module);
    const auto* found = static_cast<const table*>(PyCapsule_Import(table_name, 0));
    if (found == nullptr) throw pybind11::error_already_set();
    if (found->version < table_version)
        throw pybind11::import_error("pygim.errors' table is version " + std::to_string(found->version) +
                                     "; this module needs version " + std::to_string(table_version));
    detail::loaded = found;
}

/// The classes load() took.
[[nodiscard]] inline const table& classes() noexcept { return *detail::loaded; }

/// Raises the table's class `which`, made from `args` as Python calls it:
/// `raise(&table::option_error, value, options)` raises GimOptionError(value, options).
template <class... Args>
[[noreturn]] void raise(PyObject* table::*which, Args&&... args) {
    const pybind11::handle cls = classes().*which;
    const pybind11::object error = cls(std::forward<Args>(args)...);
    pybind11::set_error(cls, error);
    throw pybind11::error_already_set();
}

}  // namespace pygim::errors
