#pragma once
// pathlike/adapter/path_text.h — path text between Python and the internal form.
//
// The internal text of a path is UTF-8 on Windows and the system's own bytes on
// POSIX (platform/files.h, D77). A str, bytes or os.PathLike argument becomes
// that text here, and the text becomes a str here, without the fs::path round
// trip pybind11's caster would take. On POSIX a name that is not UTF-8
// round-trips, as it does through os.fsencode and os.fsdecode.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../platform/files.h"

namespace pygim::pathlike {

namespace py = pybind11;

// Whether the filesystem encoding is UTF-8, set once at module init. Then a
// str's UTF-8 buffer IS os.fsencode(str) whenever it has no lone surrogates,
// and text_view_of_arg skips the encode step (a bytes object per call).
inline bool& filesystem_is_utf8() {
    static bool utf8 = false;
    return utf8;
}

// The argument's text as a view, plus the object that keeps it alive.
struct text_arg {
    py::object keep;
    std::string_view view;
};

// str / bytes / os.PathLike -> the internal text. A str is encoded with the
// filesystem encoding on POSIX (os.fsencode) and taken as UTF-8 on Windows;
// bytes are taken as they are; anything else goes through os.fspath (TypeError
// when it cannot).
inline text_arg text_view_of_arg(py::handle obj) {
    // A str is taken as it is where the internal text and its UTF-8 agree, encoded by
    // os.fsencode where they need not.
    if constexpr (!platform::path_text_is_utf8) {
        if (filesystem_is_utf8() && PyUnicode_Check(obj.ptr())) {
            py::ssize_t n = 0;
            if (const char* s = PyUnicode_AsUTF8AndSize(obj.ptr(), &n)) {
                return {py::reinterpret_borrow<py::object>(obj), std::string_view(s, static_cast<std::size_t>(n))};
            }
            PyErr_Clear();   // lone surrogates: os.fsencode's surrogateescape below is the answer
        }
    }
    py::object p = py::reinterpret_steal<py::object>(PyOS_FSPath(obj.ptr()));   // str or bytes
    if (!p) throw py::error_already_set();
    if (PyBytes_Check(p.ptr())) {
        char* s = nullptr;
        py::ssize_t n = 0;
        if (PyBytes_AsStringAndSize(p.ptr(), &s, &n) < 0) throw py::error_already_set();
        return {std::move(p), std::string_view(s, static_cast<std::size_t>(n))};
    }
    if constexpr (platform::path_text_is_utf8) {
        py::ssize_t n = 0;
        const char* s = PyUnicode_AsUTF8AndSize(p.ptr(), &n);
        if (!s) throw py::error_already_set();
        return {std::move(p), std::string_view(s, static_cast<std::size_t>(n))};
    } else {
        py::object b = py::reinterpret_steal<py::object>(PyUnicode_EncodeFSDefault(p.ptr()));
        if (!b) throw py::error_already_set();
        char* s = nullptr;
        py::ssize_t n = 0;
        if (PyBytes_AsStringAndSize(b.ptr(), &s, &n) < 0) throw py::error_already_set();
        return {std::move(b), std::string_view(s, static_cast<std::size_t>(n))};
    }
}

// The same, copied, for a caller that keeps the text.
inline std::string text_from_arg(py::handle obj) {
    const text_arg t = text_view_of_arg(obj);
    return std::string(t.view);
}

// The internal text -> str: decoded with the filesystem encoding on POSIX
// (os.fsdecode: undecodable bytes become surrogates), as UTF-8 on Windows.
inline py::str str_from_text(std::string_view text) {
    if constexpr (platform::path_text_is_utf8) {
        return py::str(text.data(), text.size());
    } else {
        PyObject* s = PyUnicode_DecodeFSDefaultAndSize(text.data(), static_cast<py::ssize_t>(text.size()));
        if (!s) throw py::error_already_set();
        return py::reinterpret_steal<py::str>(s);
    }
}

}  // namespace pygim::pathlike
