#pragma once
// errors/table.h — the C table pygim.errors publishes: the exception classes every pygim
// extension raises, made once, in pygim.errors, and reached by every other extension through this
// table, never through pygim's Python. Only plain C data crosses (docs/design/errors.md):
// the table's version and the classes, as the interpreter's own objects.
//
// ADAPTER layer: the Python C API, no pybind11, so the module that publishes the table and the
// modules that take it agree on one layout. A module takes it through errors/adapter.h.

#include <Python.h>

#include <cstdint>
#include <type_traits>

namespace pygim::errors {

/// What a table's reader and its writer must agree on; a reader built for another version refuses
/// the table rather than read past its end.
inline constexpr std::uint32_t table_version = 1;

/// The module that publishes the table, and the capsule's name: the module and its attribute.
inline constexpr const char* module_name = "pygim.errors";
inline constexpr const char* table_name = "pygim.errors._C_API";

/// The classes, as pygim.errors made them: strong references, kept for the process's life, as the
/// interpreter keeps every extension module loaded. A new class goes at the end, with the version
/// raised, so a reader built before it still reads the classes it knows.
struct table {
    std::uint32_t version;
    PyObject* gim_exception;       ///< GimException(Exception)
    PyObject* gim_error;           ///< GimError(GimException): everything pygim raises
    PyObject* missing_key_error;   ///< MissingKeyError(GimError, KeyError): a lookup found nothing
    PyObject* option_error;        ///< GimOptionError(GimError, ValueError): a value none of its options
    PyObject* dispatch_error;      ///< DispatchError(GimError): a call could not be dispatched
    PyObject* unrecognized_type_error;   ///< UnrecognizedTypeError(DispatchError): none of the types expected
};
static_assert(std::is_standard_layout_v<table> && std::is_trivially_copyable_v<table>,
              "the table crosses between shared objects as plain C data");

}  // namespace pygim::errors
