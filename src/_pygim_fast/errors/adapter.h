#pragma once
// errors/adapter.h — how a pygim extension raises pygim's exceptions: it takes pygim.errors'
// table once, as it loads, and raises a class from it by name, with the arguments Python would
// call the class with. pygim.errors imports nothing of pygim, so taking its table from any
// module's load closes no import loop; no extension imports pygim's Python. load() also registers a
// translator, local to the extension, that turns pygim's own C++ errors (errors/core.h), thrown by
// code with no Python, into their pygim.errors classes as they reach Python.
//
// Adapter code raises every failure that reaches Python here; core code throws errors/core.h's classes,
// and std::throw_with_nested keeps the error it replaces as the Python exception's __cause__.
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

namespace detail {

/// "__notes__", interned once, on first use inside a call (so under the GIL), and never released:
/// CPython frees mortal interned strings as it finalizes.
inline const pybind11::str& notes_name() {
    static const pybind11::str* const made = [] {
        PyObject* interned = PyUnicode_InternFromString("__notes__");
        if (interned == nullptr) throw pybind11::error_already_set();
        return new pybind11::str(pybind11::reinterpret_steal<pybind11::str>(interned));
    }();
    return *made;
}

}  // namespace detail

/// Adds `note` to a Python exception, as its add_note() does (PEP 678); written out, as 3.10 has no
/// add_note and keeps __notes__ only.
inline void add_note(pybind11::handle exception, const std::string& note) {
    pybind11::object notes = pybind11::getattr(exception, detail::notes_name(), pybind11::none());
    if (notes.is_none()) {
        notes = pybind11::list();
        pybind11::setattr(exception, detail::notes_name(), notes);
    }
    if (PyList_Append(notes.ptr(), pybind11::str(note).ptr()) != 0) throw pybind11::error_already_set();
}

/// Adds `note` to the Python error in flight, as a frame it passes through: what that frame was doing.
inline void add_note(pybind11::error_already_set& in_flight, const std::string& note) {
    add_note(in_flight.value(), note);
}

namespace detail {

/// The table's class `which`, made from `args` as Python calls it, with `notes` as its __notes__.
template <class... Args>
[[nodiscard]] pybind11::object made(std::span<const std::string> notes, PyObject* table::*which, Args&&... args) {
    pybind11::object error = pybind11::handle(classes().*which)(std::forward<Args>(args)...);
    for (const std::string& note : notes) add_note(error, note);
    return error;
}

/// Sets `error`, a Python exception, as the error in flight.
inline void set_error(const pybind11::object& error) { pybind11::set_error(pybind11::type::handle_of(error), error); }

}  // namespace detail

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

inline pybind11::object python_error(const std::exception_ptr& thrown);

/// `error`, with the exception `thrown` nests (std::throw_with_nested) as its __cause__, when it
/// nests one: C++'s `raise error from cause`.
inline pybind11::object caused(pybind11::object error, const std::exception& thrown) {
    const auto* nesting = dynamic_cast<const std::nested_exception*>(&thrown);
    if (nesting == nullptr || nesting->nested_ptr() == nullptr) return error;
    if (pybind11::object cause = python_error(nesting->nested_ptr())) PyException_SetCause(error.ptr(), cause.release().ptr());
    return error;
}

/// The Python exception `thrown` becomes: each of pygim's C++ errors (errors/core.h) its
/// pygim.errors class; a Python error that a frame passed on, itself; any other std::exception the
/// RuntimeError pybind11 makes of one; anything else nothing.
inline pybind11::object python_error(const std::exception_ptr& thrown) {
    try {
        std::rethrow_exception(thrown);
    } catch (const missing_key& e) {
        return caused(made(e.notes(), &table::missing_key_error, e.key(), strings(e.keys())), e);
    } catch (const option_error& e) {
        return caused(made(e.notes(), &table::option_error, e.value(), strings(e.options())), e);
    } catch (const file_error& e) {
        return caused(made(e.notes(), class_of_errno(e.number()), e.number(), e.reason(), e.file()), e);
    } catch (const error& e) {
        return caused(made(e.notes(), kind_classes[static_cast<std::size_t>(e.meaning())].cls, e.what()), e);
    } catch (const pybind11::error_already_set& e) {
        return e.value();
    } catch (const std::exception& e) {
        return caused(pybind11::handle(PyExc_RuntimeError)(e.what()), e);
    } catch (...) {
        return {};
    }
}

/// Turns each of pygim's C++ errors into its pygim.errors class, its nested exceptions into the
/// chain of __cause__; anything else it rethrows, which hands it to the next translator, as
/// pybind11 expects of one.
inline void translate(std::exception_ptr thrown) {
    try {
        std::rethrow_exception(thrown);
    } catch (const error&) {
        set_error(python_error(thrown));
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
/// `raise(&table::option_error, value, options)` raises GimOptionError(value, options). Keyword
/// arguments pass as pybind11 writes them: `pybind11::arg("name") = name`.
template <class... Args>
[[noreturn]] void raise(PyObject* table::*which, Args&&... args) {
    detail::set_error(detail::made({}, which, std::forward<Args>(args)...));
    throw pybind11::error_already_set();
}

/// raise(), with `notes` as the exception's __notes__.
template <class... Args>
[[noreturn]] void raise_noted(std::span<const std::string> notes, PyObject* table::*which, Args&&... args) {
    detail::set_error(detail::made(notes, which, std::forward<Args>(args)...));
    throw pybind11::error_already_set();
}

/// raise(), with `cause`, the Python error that led to it, as its __cause__: Python's
/// `raise X from cause`.
template <class... Args>
[[noreturn]] void raise_from(const pybind11::error_already_set& cause, PyObject* table::*which, Args&&... args) {
    const pybind11::object error = detail::made({}, which, std::forward<Args>(args)...);
    PyException_SetCause(error.ptr(), cause.value().inc_ref().ptr());
    detail::set_error(error);
    throw pybind11::error_already_set();
}

}  // namespace pygim::errors
