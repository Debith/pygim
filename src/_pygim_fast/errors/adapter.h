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

#include <array>
#include <cerrno>
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

/// Adds `note` to a Python exception, as its add_note() does (PEP 678); written out, as 3.10 has no
/// add_note and keeps __notes__ only.
inline void add_note(pybind11::handle exception, const std::string& note) {
    const pybind11::object notes = pybind11::getattr(exception, "__notes__", pybind11::none());
    if (notes.is_none()) pybind11::setattr(exception, "__notes__", pybind11::list());
    pybind11::getattr(exception, "__notes__").attr("append")(note);
}

/// Adds `note` to the Python error in flight, as a frame it passes through: what that frame was doing.
inline void add_note(pybind11::error_already_set& in_flight, const std::string& note) {
    add_note(in_flight.value(), note);
}

/// Sets, as the Python error, the table's class `which` made from `args` as Python calls it, with
/// `notes` as its __notes__.
template <class... Args>
void set_noted(std::span<const std::string> notes, PyObject* table::*which, Args&&... args) {
    const pybind11::handle cls = classes().*which;
    const pybind11::object error = cls(std::forward<Args>(args)...);
    for (const std::string& note : notes) add_note(error, note);
    pybind11::set_error(cls, error);
}

/// Sets, as the Python error, the table's class `which` made from `args` as Python calls it.
template <class... Args>
void set(PyObject* table::*which, Args&&... args) {
    set_noted({}, which, std::forward<Args>(args)...);
}

namespace detail {

/// `texts` as a tuple of str.
inline pybind11::tuple strings(std::span<const std::string> texts) {
    pybind11::tuple made(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) made[i] = pybind11::str(texts[i]);
    return made;
}

/// A kind of pygim's C++ errors and the table's class it becomes.
struct kind_class {
    kind meaning;
    PyObject* table::*cls;
};

/// One row for each kind, in errors/core.h's order: the build fails without a row for a kind, or
/// with one out of place. A file error's class is the one its errno names (errno_classes).
inline constexpr std::array<kind_class, kinds> kind_classes{{
    {kind::error, &table::gim_error},
    {kind::missing_key, &table::missing_key_error},
    {kind::option, &table::option_error},
    {kind::argument, &table::argument_error},
    {kind::signature, &table::signature_error},
    {kind::invalid_value, &table::invalid_value_error},
    {kind::parse, &table::parse_error},
    {kind::invalid_state, &table::invalid_state_error},
    {kind::duplicate_key, &table::duplicate_key_error},
    {kind::circular_dependency, &table::circular_dependency_error},
    {kind::unsupported_column_type, &table::unsupported_column_type_error},
    {kind::backend, &table::backend_error},
    {kind::file, &table::os_error},
    {kind::overflow, &table::overflow_error},
    {kind::index_out_of_range, &table::index_out_of_range_error},
    {kind::missing_attribute, &table::missing_attribute_error},
}};
static_assert([] {
    for (std::size_t i = 0; i < kinds; ++i)
        if (kind_classes[i].meaning != static_cast<kind>(i)) return false;
    return true;
}(), "kind_classes has a row for every kind, in errors/core.h's order");

/// The errnos Python gives a class of their own, as OSError(errno, ...) picks it; any other errno
/// is a GimOSError.
struct errno_class {
    int number;
    PyObject* table::*cls;
};
inline constexpr std::array<errno_class, 5> errno_classes{{
    {ENOENT, &table::file_not_found_error},
    {EEXIST, &table::file_exists_error},
    {ENOTDIR, &table::not_a_directory_error},
    {EACCES, &table::permission_error},
    {EPERM, &table::permission_error},
}};

constexpr PyObject* table::*class_of_errno(int number) {
    for (const errno_class& row : errno_classes)
        if (row.number == number) return row.cls;
    return &table::os_error;
}

/// Turns each of pygim's C++ errors (errors/core.h) into its pygim.errors class; anything else it
/// rethrows, which hands it to the next translator, as pybind11 expects of one.
inline void translate(std::exception_ptr thrown) {
    try {
        std::rethrow_exception(thrown);
    } catch (const missing_key& e) {
        set_noted(e.notes(), &table::missing_key_error, e.key(), strings(e.keys()));
    } catch (const option_error& e) {
        set_noted(e.notes(), &table::option_error, e.value(), strings(e.options()));
    } catch (const file_error& e) {
        set_noted(e.notes(), class_of_errno(e.number()), e.number(), e.reason(), e.file());
    } catch (const error& e) {
        set_noted(e.notes(), kind_classes[static_cast<std::size_t>(e.meaning())].cls, e.what());
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
