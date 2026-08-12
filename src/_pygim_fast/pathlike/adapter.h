#pragma once
// pathlike/adapter.h — the Python-facing engine glue.
//
// Turns raw file bytes into native Python objects. This is where pybind11 and the
// vendored rapidyaml live; core.h stays free of both. The YAML tree is walked once
// and materialised into dict / list / scalars, with scalars typed by the YAML 1.2
// core schema (null, bool, int, float, else str) — quoted scalars stay strings.
//
// rapidyaml aborts the process on a parse error by default; we install a throwing
// error callback so malformed input surfaces as a Python exception instead.

#include <charconv>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "third_party/rapidyaml/ryml_all.hpp"
#include "core.h"

namespace pygim::pathlike {

namespace py = pybind11;

namespace detail {

[[nodiscard]] inline std::string_view to_sv(ryml::csubstr s) noexcept {
    return s.len ? std::string_view(s.str, s.len) : std::string_view{};
}

// rapidyaml calls this instead of aborting; rethrow as a std::exception so pybind11
// converts it to a Python RuntimeError.
[[noreturn]] inline void throw_on_error(const char* msg, size_t len, ryml::Location loc,
                                        void* /*user_data*/) {
    std::string where;
    if (loc.line) where = " (line " + std::to_string(loc.line) + ")";
    throw std::runtime_error("YAML parse error" + where + ": " + std::string(msg, len));
}

// Install the throwing error callback exactly once, process-wide.
inline void ensure_throwing_callbacks() {
    static const bool installed = [] {
        ryml::Callbacks cb = ryml::get_callbacks();
        cb.m_error = &throw_on_error;
        ryml::set_callbacks(cb);
        return true;
    }();
    (void)installed;
}

[[nodiscard]] inline bool is_null_scalar(std::string_view s) noexcept {
    return s.empty() || s == "~" || s == "null" || s == "Null" || s == "NULL";
}

[[nodiscard]] inline bool parse_bool(std::string_view s, bool& out) noexcept {
    if (s == "true" || s == "True" || s == "TRUE") { out = true; return true; }
    if (s == "false" || s == "False" || s == "FALSE") { out = false; return true; }
    return false;
}

[[nodiscard]] inline bool parse_special_float(std::string_view s, double& out) noexcept {
    std::string_view body = s;
    double sign = 1.0;
    if (!body.empty() && (body.front() == '+' || body.front() == '-')) {
        if (body.front() == '-') sign = -1.0;
        body.remove_prefix(1);
    }
    if (body == ".inf" || body == ".Inf" || body == ".INF") {
        out = sign * std::numeric_limits<double>::infinity();
        return true;
    }
    if (s == ".nan" || s == ".NaN" || s == ".NAN") {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    return false;
}

// A single YAML scalar -> the Python object it denotes. Quoted scalars are always
// strings (the author asked for text); unquoted scalars are type-inferred.
[[nodiscard]] inline py::object scalar_to_py(std::string_view s, bool quoted) {
    if (quoted) return py::str(std::string(s));
    if (is_null_scalar(s)) return py::none();

    bool b = false;
    if (parse_bool(s, b)) return py::bool_(b);

    std::string_view digits = s;
    if (!digits.empty() && digits.front() == '+') digits.remove_prefix(1);
    long long i = 0;
    auto ires = std::from_chars(digits.data(), digits.data() + digits.size(), i);
    if (ires.ec == std::errc() && ires.ptr == digits.data() + digits.size()) {
        return py::int_(i);
    }

    double d = 0.0;
    if (parse_special_float(s, d)) return py::float_(d);
    auto fres = std::from_chars(s.data(), s.data() + s.size(), d);
    if (fres.ec == std::errc() && fres.ptr == s.data() + s.size()) {
        return py::float_(d);
    }

    return py::str(std::string(s));
}

// Recursively materialise a rapidyaml node as a native Python object.
py::object node_to_py(ryml::ConstNodeRef node);  // fwd (mutual recursion via children)

[[nodiscard]] inline py::object map_to_py(ryml::ConstNodeRef node) {
    py::dict out;
    for (ryml::ConstNodeRef child : node.children()) {
        py::object key = scalar_to_py(to_sv(child.key()), child.is_key_quoted());
        out[key] = node_to_py(child);
    }
    return out;
}

[[nodiscard]] inline py::object seq_to_py(ryml::ConstNodeRef node) {
    py::list out;
    for (ryml::ConstNodeRef child : node.children()) out.append(node_to_py(child));
    return out;
}

inline py::object node_to_py(ryml::ConstNodeRef node) {
    if (node.is_stream()) return seq_to_py(node);   // a multi-document stream -> list of docs
    if (node.is_map()) return map_to_py(node);
    if (node.is_seq()) return seq_to_py(node);
    if (node.has_val()) return scalar_to_py(to_sv(node.val()), node.is_val_quoted());
    return py::none();                              // empty document
}

[[nodiscard]] inline py::object load_yaml(std::string_view text) {
    ensure_throwing_callbacks();
    ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(text.data(), text.size()));
    tree.resolve();                                 // expand anchors / *aliases
    return node_to_py(tree.crootref());
}

}  // namespace detail

// Read `f` and decode it with `engine`. YAML is native C++ (rapidyaml); JSON is
// reserved for a future simdjson engine and refuses loudly until then.
[[nodiscard]] inline py::object load(const file& f, Engine engine) {
    switch (engine) {
        case Engine::Yaml:
            return detail::load_yaml(f.read_bytes());
        case Engine::Json:
            throw std::runtime_error("json engine not implemented yet "
                                     "(the yaml engine is the first to land)");
        case Engine::Unknown:
            break;
    }
    throw std::invalid_argument("no engine resolved for " + f.fspath());
}

}  // namespace pygim::pathlike
