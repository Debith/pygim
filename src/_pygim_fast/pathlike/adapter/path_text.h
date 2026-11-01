#pragma once
// pathlike/adapter/path_text.h — a Python path argument as the internal path text, and back. Every
// extension that takes a path from Python reads it here: pathlike's own classes, code.parse,
// similarity's add_file and add_files.
//
// ADAPTER layer.

#include <string>
#include <string_view>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../platform/files.h"

namespace pygim::pathlike {

namespace py = pybind11;

// Python str / bytes / os.PathLike -> the internal path text, without the
// fs::path round trip pybind11's caster would take: str is encoded as the
// internal text is (platform::path_text_is_utf8) — with the filesystem
// encoding on POSIX (os.fsencode, so non-UTF-8 names round-trip), as UTF-8 on
// Windows (what text_from_fs yields there); bytes are taken as they are;
// anything else goes through os.fspath (TypeError when it cannot).
// Whether the filesystem encoding is UTF-8 (set once at module init): then a
// str's UTF-8 buffer IS os.fsencode(str) whenever it has no lone surrogates,
// and the encode step (a bytes object per call) can be skipped.
inline bool& filesystem_is_utf8() {
    static bool utf8 = false;
    return utf8;
}

// The argument's text as a view, and the object that keeps it alive.
class text_arg {
public:
    text_arg(py::object keep, std::string_view text) noexcept : m_keep(std::move(keep)), m_text(text) {}
    [[nodiscard]] std::string_view view() const noexcept { return m_text; }

private:
    py::object m_keep;        // owns the text
    std::string_view m_text;
};
inline text_arg text_view_of_arg(py::handle obj) {
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
inline std::string text_from_arg(py::handle obj) {
    const text_arg t = text_view_of_arg(obj);
    return std::string(t.view());
}

// Internal text -> Python str: the native byte string is decoded with the
// filesystem encoding on POSIX (os.fsdecode, so non-UTF-8 names round-trip);
// on Windows the internal text is UTF-8 already.
inline py::str str_from_text(std::string_view text) {
    if constexpr (platform::path_text_is_utf8) {
        return py::str(text.data(), text.size());
    } else {
        py::str decoded = py::reinterpret_steal<py::str>(
            PyUnicode_DecodeFSDefaultAndSize(text.data(), static_cast<py::ssize_t>(text.size())));
        if (!decoded) throw py::error_already_set();
        return decoded;
    }
}

// os.fspath(p) as UTF-8 text: a path as it is shown — an origin in results, a name in an error —
// where text_from_arg is the path as the OS takes it. A bytes path is read as os.fsdecode reads
// it, so a name the filesystem encoding cannot decode (lone surrogates, from a str or bytes alike)
// is a UnicodeEncodeError, never text with invalid bytes (an Arrow utf8 column would hold them).
inline std::string utf8_fspath(py::handle p) {
    py::object s = py::reinterpret_steal<py::object>(PyOS_FSPath(p.ptr()));
    if (!s) throw py::error_already_set();
    if (PyBytes_Check(s.ptr())) {
        s = py::reinterpret_steal<py::object>(PyUnicode_DecodeFSDefaultAndSize(PyBytes_AS_STRING(s.ptr()), PyBytes_GET_SIZE(s.ptr())));
        if (!s) throw py::error_already_set();
    }
    Py_ssize_t n = 0;
    const char* utf8 = PyUnicode_AsUTF8AndSize(s.ptr(), &n);
    if (utf8 == nullptr) throw py::error_already_set();
    return std::string(utf8, static_cast<std::size_t>(n));
}

}  // namespace pygim::pathlike
