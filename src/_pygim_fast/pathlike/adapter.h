#pragma once
// pathlike/adapter.h — the Python-facing engine glue.
//
// Turns raw file bytes into native Python objects. This is where pybind11 and the
// vendored engines (rapidyaml for YAML, simdjson for JSON) live; core.h stays free
// of all three. The YAML tree is walked once and materialised into dict / list /
// scalars, with scalars typed by the YAML 1.2 core schema (null, bool, int, float,
// else str) — quoted scalars stay strings. JSON is materialised from simdjson's DOM.
//
// rapidyaml aborts the process on a parse error by default; we install a throwing
// error callback so malformed input surfaces as a Python exception instead.

#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "third_party/rapidyaml/ryml_all.hpp"
#include "third_party/simdjson/simdjson.h"
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

// YAML 1.2 core schema spells each keyword exactly three ways: word, Word,
// WORD — never mixed case. A case-folding trick would accept spellings the
// schema rejects (".iNf"), so matching is against the three exact variants.
struct YamlWord {
    std::string_view lower, title, upper;
};

inline constexpr YamlWord         kNull {"null",  "Null",  "NULL"};
inline constexpr YamlWord         kTrue {"true",  "True",  "TRUE"};
inline constexpr YamlWord         kFalse{"false", "False", "FALSE"};
inline constexpr YamlWord         kInf  {".inf",  ".Inf",  ".INF"};
inline constexpr YamlWord         kNan  {".nan",  ".NaN",  ".NAN"};
inline constexpr std::string_view kNullTilde{"~"};

// Each triple must hang together: lower is all lower-case, upper is its exact
// upper-casing, and the middle form is a distinct re-casing of the same word.
// (The middle form is not uniformly Title-case — the schema says Inf but NaN —
// so its exact spelling is pinned by the behavior asserts below instead.)
consteval bool word_variants_consistent(const YamlWord& w) {
    if (w.lower.empty()) return false;
    if (w.lower.size() != w.title.size() || w.lower.size() != w.upper.size()) return false;
    for (std::size_t i = 0; i < w.lower.size(); ++i) {
        const char lo = w.lower[i];
        if (lo >= 'A' && lo <= 'Z') return false;                // lower is lower-case
        const bool is_alpha = lo >= 'a' && lo <= 'z';
        const char up = is_alpha ? static_cast<char>(lo - 'a' + 'A') : lo;
        if (w.upper[i] != up) return false;                      // upper is UPPER of lower
        if (w.title[i] != lo && w.title[i] != up) return false;  // title re-cases the word
    }
    return w.title != w.lower;                                   // and is a distinct form
}
static_assert(word_variants_consistent(kNull) && word_variants_consistent(kTrue) &&
              word_variants_consistent(kFalse) && word_variants_consistent(kInf) &&
              word_variants_consistent(kNan));

// One length gate plus at most three word-sized compares.
[[nodiscard]] constexpr bool is_yaml_word(std::string_view s, const YamlWord& w) noexcept {
    return s == w.lower || s == w.title || s == w.upper;
}

[[nodiscard]] constexpr bool is_null_scalar(std::string_view s) noexcept {
    return s.empty() || s == kNullTilde || is_yaml_word(s, kNull);
}

[[nodiscard]] constexpr bool parse_bool(std::string_view s, bool& out) noexcept {
    if (is_yaml_word(s, kTrue)) { out = true; return true; }
    if (is_yaml_word(s, kFalse)) { out = false; return true; }
    return false;
}

[[nodiscard]] constexpr bool parse_special_float(std::string_view s, double& out) noexcept {
    std::string_view body = s;
    double sign = 1.0;
    if (!body.empty() && (body.front() == '+' || body.front() == '-')) {
        if (body.front() == '-') sign = -1.0;
        body.remove_prefix(1);
    }
    if (is_yaml_word(body, kInf)) {
        out = sign * std::numeric_limits<double>::infinity();
        return true;
    }
    if (is_yaml_word(s, kNan)) {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    return false;
}

// ── Compile-time proof of the core-schema scalar rules ─────────────────────
// YAML 1.2 core schema: these exact spellings and no others — the YAML 1.1
// forms (yes/no/on/off, y/n) deliberately stay strings.

consteval bool bool_is(std::string_view s, bool expect) {
    bool b{};
    return parse_bool(s, b) && b == expect;
}
consteval bool bool_rejected(std::string_view s) {
    bool b{};
    return !parse_bool(s, b);
}
consteval bool inf_is(std::string_view s, bool negative) {
    double d{};
    return parse_special_float(s, d) &&
           d == (negative ? -1.0 : 1.0) * std::numeric_limits<double>::infinity();
}
consteval bool nan_is(std::string_view s) {
    double d{};
    return parse_special_float(s, d) && d != d;   // NaN is the only value != itself
}
consteval bool float_rejected(std::string_view s) {
    double d{};
    return !parse_special_float(s, d);
}

static_assert(is_null_scalar("") && is_null_scalar("~"));
static_assert(is_null_scalar("null") && is_null_scalar("Null") && is_null_scalar("NULL"));
static_assert(!is_null_scalar("NuLL") && !is_null_scalar("nil") && !is_null_scalar("None"));

static_assert(bool_is("true", true) && bool_is("True", true) && bool_is("TRUE", true));
static_assert(bool_is("false", false) && bool_is("False", false) && bool_is("FALSE", false));
static_assert(bool_rejected("yes") && bool_rejected("no") && bool_rejected("on") &&
              bool_rejected("off") && bool_rejected("1") && bool_rejected("tRue"));

static_assert(inf_is(".inf", false) && inf_is(".Inf", false) && inf_is(".INF", false));
static_assert(inf_is("+.inf", false) && inf_is("-.inf", true) && inf_is("-.INF", true));
static_assert(nan_is(".nan") && nan_is(".NaN") && nan_is(".NAN"));
static_assert(float_rejected("inf") && float_rejected("nan") && float_rejected("-.nan") &&
              float_rejected(".infinity") && float_rejected(".INFx"));

// YAML 1.2 core-schema integer forms: [-+]?[0-9]+, 0x[0-9a-fA-F]+, 0o[0-7]+.
// The classifier is constexpr (and proven below); materialisation is done by
// CPython itself so integers of any magnitude convert exactly.
[[nodiscard]] constexpr bool is_core_int(std::string_view s) noexcept {
    constexpr auto all = [](std::string_view t, auto pred) {
        if (t.empty()) return false;
        for (char c : t) {
            if (!pred(c)) return false;
        }
        return true;
    };
    if (s.starts_with("0x")) {
        return all(s.substr(2), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
    }
    if (s.starts_with("0o")) {
        return all(s.substr(2), [](char c) { return c >= '0' && c <= '7'; });
    }
    std::string_view body = s;
    if (!body.empty() && (body.front() == '+' || body.front() == '-')) body.remove_prefix(1);
    return all(body, [](char c) { return c >= '0' && c <= '9'; });
}

static_assert(is_core_int("0") && is_core_int("-12") && is_core_int("+12") &&
              is_core_int("010") && is_core_int("12345678901234567890123"));
static_assert(is_core_int("0x1A") && is_core_int("0xff") && is_core_int("0o17"));
static_assert(!is_core_int("") && !is_core_int("-") && !is_core_int("0x") &&
              !is_core_int("0o") && !is_core_int("0o8") && !is_core_int("0xGG"));
static_assert(!is_core_int("1.5") && !is_core_int("1e3") && !is_core_int("0b1") &&
              !is_core_int("1_000") && !is_core_int("-0x1A"));   // 1.1-isms stay strings

// YAML 1.2 core-schema float form: [-+]? ( \.[0-9]+ | [0-9]+(\.[0-9]*)? )
// ( [eE][-+]?[0-9]+ )?. The gate matters: a bare from_chars/strtod fallback
// would also accept "inf"/"nan"/"infinity", which the schema says are strings
// (only the dot-forms ".inf"/".nan" are special, handled separately above).
[[nodiscard]] constexpr bool is_core_float(std::string_view s) noexcept {
    std::string_view b = s;
    if (!b.empty() && (b.front() == '+' || b.front() == '-')) b.remove_prefix(1);
    if (b.empty()) return false;
    std::size_t i = 0;
    bool int_digits = false, frac_digits = false, has_dot = false;
    while (i < b.size() && b[i] >= '0' && b[i] <= '9') { ++i; int_digits = true; }
    if (i < b.size() && b[i] == '.') {
        has_dot = true;
        ++i;
        while (i < b.size() && b[i] >= '0' && b[i] <= '9') { ++i; frac_digits = true; }
    }
    if (!int_digits && !frac_digits) return false;   // needs a digit somewhere
    if (has_dot && !int_digits && !frac_digits) return false;
    if (i < b.size() && (b[i] == 'e' || b[i] == 'E')) {
        ++i;
        if (i < b.size() && (b[i] == '+' || b[i] == '-')) ++i;
        bool exp_digits = false;
        while (i < b.size() && b[i] >= '0' && b[i] <= '9') { ++i; exp_digits = true; }
        if (!exp_digits) return false;
    }
    return i == b.size();
}

static_assert(is_core_float("2.5") && is_core_float("-.5") && is_core_float(".5") &&
              is_core_float("5.") && is_core_float("0.") && is_core_float("+0.5e+2"));
static_assert(is_core_float("1e3") && is_core_float("-1E-3") && is_core_float("1.5e10"));
static_assert(!is_core_float("") && !is_core_float(".") && !is_core_float("-") &&
              !is_core_float("e3") && !is_core_float(".e3") && !is_core_float("1e") &&
              !is_core_float("1e+") && !is_core_float("1.5.2") && !is_core_float("1 "));
static_assert(!is_core_float("inf") && !is_core_float("nan") && !is_core_float("infinity") &&
              !is_core_float("INF") && !is_core_float("0x1A"));   // strtod-isms stay strings

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

// ── JSON engine (simdjson) ─────────────────────────────────────────────────
// Recursively materialise a simdjson DOM element as a native Python object.
// JSON semantics are exact: object keys are strings, numbers arrive already
// typed by the parser (int64 / uint64 / double).
[[nodiscard]] inline py::object json_to_py(simdjson::dom::element el) {
    using simdjson::dom::element_type;
    switch (el.type()) {
        case element_type::OBJECT: {
            py::dict out;
            for (auto [key, value] : simdjson::dom::object(el)) {
                out[py::str(std::string(key))] = json_to_py(value);
            }
            return out;
        }
        case element_type::ARRAY: {
            py::list out;
            for (simdjson::dom::element child : simdjson::dom::array(el)) {
                out.append(json_to_py(child));
            }
            return out;
        }
        case element_type::STRING: return py::str(std::string(std::string_view(el)));
        case element_type::INT64:  return py::int_(int64_t(el));
        case element_type::UINT64: return py::int_(uint64_t(el));
        case element_type::DOUBLE: return py::float_(double(el));
        case element_type::BOOL:   return py::bool_(bool(el));
        case element_type::NULL_VALUE: return py::none();
    }
    throw std::runtime_error("json: unhandled element type");
}

[[nodiscard]] inline py::object load_json(const std::string& fspath) {
    try {
        simdjson::dom::parser parser;
        return json_to_py(parser.load(fspath));
    } catch (const simdjson::simdjson_error& e) {
        throw std::runtime_error("JSON parse error (" + fspath + "): " + e.what());
    }
}

}  // namespace detail

// Read `f` and decode it with `engine`. YAML is native C++ (rapidyaml); JSON
// is native C++ too (simdjson, SIMD-accelerated).
[[nodiscard]] inline py::object load(const file& f, Engine engine) {
    switch (engine) {
        case Engine::Yaml:
            return detail::load_yaml(f.read_bytes());
        case Engine::Json:
            return detail::load_json(f.fspath());
        case Engine::Unknown:
            break;
    }
    throw std::invalid_argument("no engine resolved for " + f.fspath());
}

}  // namespace pygim::pathlike
