// wiring/common/adapter_errors.h
// The wiring modules' errors as pygim's (pygim.core.explib), each also the Python exception its
// contract needs: a lookup that found nothing (core::missing_key) is MissingKeyError, a KeyError;
// a value none of its options (core::unknown_option) is GimOptionError, a ValueError. Registered by
// each module for itself, so no other extension's errors are renamed.

#pragma once

#include <pybind11/pybind11.h>

#include <exception>

#include "../../utils/adapter_utils.h"
#include "errors.h"

namespace pygim::wiring::detail {

namespace py = pybind11;

/// Fetches explib's classes once, as the module loads (never while running), and raises each
/// wiring error thrown from this module's functions as its class; anything else goes on to the
/// next translator. One rethrow and catch per exception, on the failure path only. pybind11 takes
/// a plain function pointer, so the classes are held in statics, one set in each module's copy;
/// they are kept for the process's life, as the module keeps explib imported.
inline void register_wiring_errors() {
    static PyObject* missing_key_error = nullptr;
    static PyObject* option_error = nullptr;
    const py::module_ explib = py::module_::import("pygim.core.explib");
    missing_key_error = py::object(explib.attr(adapter::interned<"MissingKeyError">())).release().ptr();
    option_error = py::object(explib.attr(adapter::interned<"GimOptionError">())).release().ptr();
    py::register_local_exception_translator([](std::exception_ptr p) {
        if (!p) return;
        try {
            std::rethrow_exception(p);
        } catch (const core::missing_key& e) {
            py::set_error(missing_key_error, e.what());
        } catch (const core::unknown_option& e) {
            py::list options;
            for (const std::string& option : e.options()) options.append(option);
            const py::object error = py::reinterpret_borrow<py::object>(option_error)(e.value(), options);
            PyErr_SetObject(option_error, error.ptr());
        }
    });
}

} // namespace pygim::wiring::detail
