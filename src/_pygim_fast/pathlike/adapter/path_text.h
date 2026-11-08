#pragma once
// pathlike/adapter/path_text.h — path text between Python and the internal form.
//
// The internal text of a path is the system's own bytes on POSIX and UTF-8 on
// Windows (platform/files.h, D77). A str, bytes or os.PathLike argument becomes
// that text here, and the text becomes a str here, without the fs::path round
// trip pybind11's caster would take. One class per family (D106): posix_path_text
// goes through os.fsencode and os.fsdecode, so a name that is not UTF-8
// round-trips; windows_path_text takes a str's UTF-8 as it is. Both compile on
// every system; the family chooses one, once, as platform/native.h says.

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../platform/native.h"

namespace pygim::pathlike {

namespace py = pybind11;

// Whether the filesystem encoding is UTF-8, set once at module init. Then a
// str's UTF-8 buffer IS os.fsencode(str) whenever it has no lone surrogates,
// and posix_path_text skips the encode step (a bytes object per call).
inline bool& filesystem_is_utf8() {
    static bool utf8 = false;
    return utf8;
}

// The argument's text as a view, plus the object that keeps it alive.
struct text_arg {
    py::object keep;
    std::string_view view;
};

namespace path_text_detail {

/// os.fspath(obj): a str or bytes, or the TypeError os.fspath raises.
inline py::object fspath(py::handle obj) {
    py::object p = py::reinterpret_steal<py::object>(PyOS_FSPath(obj.ptr()));
    if (!p) throw py::error_already_set();
    return p;
}

/// A bytes object's buffer, kept alive by the object.
inline text_arg view_of_bytes(py::object bytes) {
    char* s = nullptr;
    py::ssize_t n = 0;
    if (PyBytes_AsStringAndSize(bytes.ptr(), &s, &n) < 0) throw py::error_already_set();
    return {std::move(bytes), std::string_view(s, static_cast<std::size_t>(n))};
}

/// A str's UTF-8 buffer, kept alive by the str; a lone surrogate is the UnicodeEncodeError it raises.
inline text_arg view_of_utf8(py::object str) {
    py::ssize_t n = 0;
    const char* s = PyUnicode_AsUTF8AndSize(str.ptr(), &n);
    if (!s) throw py::error_already_set();
    return {std::move(str), std::string_view(s, static_cast<std::size_t>(n))};
}

}  // namespace path_text_detail

/// POSIX: the internal text is the system's own bytes. A str is encoded with the filesystem encoding
/// (os.fsencode, surrogateescape), or taken as its UTF-8 when that encoding is UTF-8 and the str has
/// no lone surrogate; bytes are taken as they are; text comes back through os.fsdecode, undecodable
/// bytes becoming surrogates.
struct posix_path_text {
    [[nodiscard]] static text_arg view_of_arg(py::handle obj) {
        if (filesystem_is_utf8() && PyUnicode_Check(obj.ptr())) {
            py::ssize_t n = 0;
            if (const char* s = PyUnicode_AsUTF8AndSize(obj.ptr(), &n)) {
                return {py::reinterpret_borrow<py::object>(obj), std::string_view(s, static_cast<std::size_t>(n))};
            }
            PyErr_Clear();   // lone surrogates: os.fsencode's surrogateescape below is the answer
        }
        py::object p = path_text_detail::fspath(obj);
        if (PyBytes_Check(p.ptr())) return path_text_detail::view_of_bytes(std::move(p));
        py::object b = py::reinterpret_steal<py::object>(PyUnicode_EncodeFSDefault(p.ptr()));
        if (!b) throw py::error_already_set();
        return path_text_detail::view_of_bytes(std::move(b));
    }
    [[nodiscard]] static py::str str_from_text(std::string_view text) {
        PyObject* s = PyUnicode_DecodeFSDefaultAndSize(text.data(), static_cast<py::ssize_t>(text.size()));
        if (!s) throw py::error_already_set();
        return py::reinterpret_steal<py::str>(s);
    }
};

/// Windows: the internal text is UTF-8. A str's UTF-8 buffer is the text; bytes are taken as they
/// are; text comes back as a str from UTF-8.
struct windows_path_text {
    [[nodiscard]] static text_arg view_of_arg(py::handle obj) {
        py::object p = path_text_detail::fspath(obj);
        if (PyBytes_Check(p.ptr())) return path_text_detail::view_of_bytes(std::move(p));
        return path_text_detail::view_of_utf8(std::move(p));
    }
    [[nodiscard]] static py::str str_from_text(std::string_view text) { return py::str(text.data(), text.size()); }
};

/// The running system's rules, chosen once by the family (platform/native.h).
using path_text = std::conditional_t<platform::native == platform::family::windows, windows_path_text, posix_path_text>;

// str / bytes / os.PathLike -> the internal text.
inline text_arg text_view_of_arg(py::handle obj) { return path_text::view_of_arg(obj); }

// The same, copied, for a caller that keeps the text.
inline std::string text_from_arg(py::handle obj) {
    const text_arg t = text_view_of_arg(obj);
    return std::string(t.view);
}

// The internal text -> str.
inline py::str str_from_text(std::string_view text) { return path_text::str_from_text(text); }

}  // namespace pygim::pathlike
