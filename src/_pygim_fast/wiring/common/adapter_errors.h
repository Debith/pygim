// wiring/common/adapter_errors.h
// The wiring modules' errors as Python's own: a lookup that found nothing (core::missing_key) is
// KeyError. Registered by each module for itself, so no other extension's errors are renamed.

#pragma once

#include <pybind11/pybind11.h>

#include <exception>

#include "errors.h"

namespace pygim::wiring::detail {

namespace py = pybind11;

/// Raises every `CppError` thrown from this module's functions as `python_type`, with its
/// message; anything else goes on to the next translator. One rethrow and catch per exception,
/// on the failure path only. pybind11 takes a plain function pointer, so the type is held in a
/// static of this instantiation — one per error type, in each module's own copy.
template <class CppError>
void translate_locally(PyObject* python_type) {
    static PyObject* target = nullptr;
    target = python_type;
    py::register_local_exception_translator([](std::exception_ptr p) {
        if (!p) return;
        try {
            std::rethrow_exception(p);
        } catch (const CppError& e) {
            py::set_error(target, e.what());
        }
    });
}

/// What every wiring module registers: a missing key is KeyError.
inline void register_wiring_errors() { translate_locally<core::missing_key>(PyExc_KeyError); }

} // namespace pygim::wiring::detail
