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
inline constexpr std::uint32_t table_version = 2;

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
    PyObject* unrecognized_type_error;   ///< UnrecognizedTypeError(GimError, TypeError): none of the types expected
    // Version 2:
    PyObject* argument_error;            ///< ArgumentError(GimError, TypeError): a call pygim cannot take as given
    PyObject* signature_error;           ///< SignatureError(GimError, TypeError): a callable whose signature pygim cannot use
    PyObject* invalid_value_error;       ///< InvalidValueError(GimError, ValueError): the right type, outside what is accepted
    PyObject* parse_error;               ///< ParseError(GimError, ValueError): text that is not its format
    PyObject* invalid_state_error;       ///< InvalidStateError(GimError, RuntimeError): not allowed in the object's state
    PyObject* duplicate_key_error;       ///< DuplicateKeyError(GimError, ValueError): a key registered twice
    PyObject* circular_dependency_error; ///< CircularDependencyError(GimError, ValueError): wiring that depends on itself
    PyObject* unsupported_column_type_error;   ///< UnsupportedColumnTypeError(GimError, TypeError): a column type pygim cannot map
    PyObject* backend_error;             ///< BackendError(GimError, RuntimeError): a library or driver pygim calls failed
    PyObject* os_error;                  ///< GimOSError(GimError, OSError): the system refused a file operation
    PyObject* file_not_found_error;      ///< GimFileNotFoundError(GimOSError, FileNotFoundError)
    PyObject* file_exists_error;         ///< GimFileExistsError(GimOSError, FileExistsError)
    PyObject* not_a_directory_error;     ///< GimNotADirectoryError(GimOSError, NotADirectoryError)
    PyObject* permission_error;          ///< GimPermissionError(GimOSError, PermissionError)
    PyObject* overflow_error;            ///< GimOverflowError(GimError, OverflowError): past what its type holds
    PyObject* index_out_of_range_error;  ///< IndexOutOfRangeError(GimError, IndexError): a position past the end
    PyObject* missing_attribute_error;   ///< MissingAttributeError(GimError, AttributeError): an attribute that is not there
};
static_assert(std::is_standard_layout_v<table> && std::is_trivially_copyable_v<table>,
              "the table crosses between shared objects as plain C data");

}  // namespace pygim::errors
