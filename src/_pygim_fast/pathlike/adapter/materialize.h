#pragma once
// pathlike/adapter/materialize.h — scalar decisions -> Python values.
//
// The thin, shared half of the adapter: core scalars.h decides WHAT a scalar
// is (pybind-free, compile-time proven); this file turns those decisions into
// py::objects — CPython does the numeric conversions so values are exact —
// and interns repeated mapping keys (KeyCache).

#include <string>
#include <string_view>
#include <unordered_map>

#include <pybind11/pybind11.h>

#include "../scalars.h"

namespace pygim::pathlike {

namespace py = pybind11;

namespace detail {

// Materialise a core-schema integer via CPython (arbitrary precision, exact).
// Raw C API on purpose: Python's int(str) needs an explicit base for "0x1A",
// and pybind11 has no wrapper for string->int with a base — PyLong_FromString
// is exactly that call. (py::int_'s converting ctor is PyNumber_Long, which
// rejects prefixed literals.)
[[nodiscard]] inline py::object make_int(std::string_view s) {
    const int base = s.starts_with("0x") ? 16 : s.starts_with("0o") ? 8 : 10;
    PyObject* obj = PyLong_FromString(std::string(s).c_str(), nullptr, base);
    if (!obj) throw py::error_already_set();   // unreachable after is_core_int()
    return py::reinterpret_steal<py::object>(obj);
}

// Materialise a core-schema float: float(str) via py::float_'s converting
// constructor (PyNumber_Float — CPython's correctly-rounded parser; overflow
// saturates to inf, underflow to 0.0, exactly like float("1e999")). The gate
// above decides *what* is a float; CPython decides its value.
[[nodiscard]] inline py::object make_float(std::string_view s) {
    return py::float_(py::str(s));
}

// Per-read() key interning cache: documents with repeated mapping keys (rows
// of records) reuse one py::str per distinct key instead of allocating each
// time. `capacity` bounds distinct entries (0 disables; beyond capacity keys
// still convert, they just aren't remembered).
class KeyCache {
public:
    explicit KeyCache(std::size_t capacity) : m_capacity(capacity) {}

    [[nodiscard]] py::str get(std::string_view key) {
        if (m_capacity == 0) return py::str(key);
        if (auto it = m_map.find(key); it != m_map.end()) return it->second;
        py::str s(key);
        if (m_map.size() < m_capacity) m_map.emplace(key, s);
        return s;
    }

private:
    struct sv_hash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    std::size_t m_capacity;
    std::unordered_map<std::string, py::str, sv_hash, std::equal_to<>> m_map;
};

// A single YAML scalar -> the Python object it denotes. Quoted scalars are always
// strings (the author asked for text); unquoted scalars are type-inferred.
[[nodiscard]] inline py::object scalar_to_py(std::string_view s, bool quoted) {
    if (quoted) return py::str(std::string(s));
    if (is_null_scalar(s)) return py::none();

    bool b = false;
    if (parse_bool(s, b)) return py::bool_(b);

    if (is_core_int(s)) return make_int(s);

    double d = 0.0;
    if (parse_special_float(s, d)) return py::float_(d);
    if (is_core_float(s)) return make_float(s);

    return py::str(std::string(s));
}

}  // namespace detail
}  // namespace pygim::pathlike
