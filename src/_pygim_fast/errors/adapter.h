#pragma once
// errors/adapter.h — how a pygim extension raises pygim's exceptions: it takes pygim.errors'
// table once, as it loads, and raises a class from it by name, with the arguments Python would
// call the class with. pygim.errors imports nothing of pygim, so taking its table from any
// module's load closes no import loop; no extension imports pygim's Python. load() also registers a
// translator, local to the extension, that turns pygim's own C++ errors (errors/core.h), thrown by
// code with no Python, into their pygim.errors classes as they reach Python.
//
// ADAPTER layer.
//
//     PYBIND11_MODULE(utils, m) {
//         pygim::errors::load();
//         ...
//     pygim::errors::raise(&pygim::errors::table::option_error, value, options);
//     // GimOptionError: Given input ``x`` not among: sum, max, min, replace

#include "core.h"
#include "table.h"

#include <pybind11/pybind11.h>

#include <exception>
#include <span>
#include <string>
#include <utility>

namespace pygim::errors {

namespace detail {

/// This module's copy of the table's address, set by load() and read by every raise after it.
inline const table* loaded = nullptr;

}  // namespace detail

/// The classes load() took.
[[nodiscard]] inline const table& classes() noexcept { return *detail::loaded; }

/// Sets, as the Python error, the table's class `which` made from `args` as Python calls it.
template <class... Args>
void set(PyObject* table::*which, Args&&... args) {
    const pybind11::handle cls = classes().*which;
    const pybind11::object error = cls(std::forward<Args>(args)...);
    pybind11::set_error(cls, error);
}

namespace detail {

/// `texts` as a tuple of str.
inline pybind11::tuple strings(std::span<const std::string> texts) {
    pybind11::tuple made(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) made[i] = pybind11::str(texts[i]);
    return made;
}

/// Turns each of pygim's C++ errors (errors/core.h) into its pygim.errors class; anything else it
/// rethrows, which hands it to the next translator, as pybind11 expects of one.
inline void translate(std::exception_ptr thrown) {
    try {
        std::rethrow_exception(thrown);
    } catch (const missing_key& e) {
        set(&table::missing_key_error, e.key(), strings(e.keys()));
    } catch (const option_error& e) {
        set(&table::option_error, e.value(), strings(e.options()));
    } catch (const error& e) {
        set(&table::gim_error, e.what());
    }
}

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
    pybind11::register_local_exception_translator(&detail::translate);
}

/// Raises the table's class `which`, made from `args` as Python calls it:
/// `raise(&table::option_error, value, options)` raises GimOptionError(value, options).
template <class... Args>
[[noreturn]] void raise(PyObject* table::*which, Args&&... args) {
    set(which, std::forward<Args>(args)...);
    throw pybind11::error_already_set();
}

}  // namespace pygim::errors
