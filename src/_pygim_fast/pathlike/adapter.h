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

#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>

#include <pybind11/pybind11.h>

#include "third_party/rapidyaml/ryml_all.hpp"
#include "third_party/simdjson/simdjson.h"
#define TOML_EXCEPTIONS 0   // error-code API (toml::parse_result), no throw across nogil
#include "third_party/tomlplusplus/toml.hpp"
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

// True when an unquoted scalar resolves to a plain string (i.e. none of the
// core-schema classifiers claim it). Also the emit-side quoting rule: a
// string value that is NOT plain must be quoted, or it would read back typed.
[[nodiscard]] constexpr bool scalar_is_string(std::string_view s) noexcept {
    bool b = false;
    double d = 0.0;
    return !is_null_scalar(s) && !parse_bool(s, b) && !is_core_int(s) &&
           !is_core_float(s) && !parse_special_float(s, d);
}

static_assert(scalar_is_string("hello") && scalar_is_string("yes") &&
              scalar_is_string("0b1") && scalar_is_string("inf"));
static_assert(!scalar_is_string("null") && !scalar_is_string("true") &&
              !scalar_is_string("0x1A") && !scalar_is_string("2.5") &&
              !scalar_is_string(".inf") && !scalar_is_string(""));

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

// Recursively materialise a rapidyaml node as a native Python object.
py::object node_to_py(ryml::ConstNodeRef node, KeyCache& keys);  // fwd (mutual recursion)

[[nodiscard]] inline py::object map_to_py(ryml::ConstNodeRef node, KeyCache& keys) {
    py::dict out;
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string_view k = to_sv(child.key());
        // String keys (quoted, or unquoted-but-plain) go through the interning
        // cache; typed keys (ints, bools, ...) resolve like any scalar.
        py::object key = (child.is_key_quoted() || scalar_is_string(k))
                             ? py::object(keys.get(k))
                             : scalar_to_py(k, false);
        out[key] = node_to_py(child, keys);
    }
    return out;
}

[[nodiscard]] inline py::object seq_to_py(ryml::ConstNodeRef node, KeyCache& keys) {
    py::list out;
    for (ryml::ConstNodeRef child : node.children()) out.append(node_to_py(child, keys));
    return out;
}

inline py::object node_to_py(ryml::ConstNodeRef node, KeyCache& keys) {
    if (node.is_stream()) return seq_to_py(node, keys);   // multi-doc stream -> list of docs
    if (node.is_map()) return map_to_py(node, keys);
    if (node.is_seq()) return seq_to_py(node, keys);
    if (node.has_val()) return scalar_to_py(to_sv(node.val()), node.is_val_quoted());
    return py::none();                                    // empty document
}

// File I/O and parsing run with the GIL RELEASED (pure C++; the throwing ryml
// callback is GIL-free too), so reads scale across Python threads. Only the
// materialisation into Python objects reacquires the GIL.
[[nodiscard]] inline py::object load_yaml(const file& f, KeyCache& keys) {
    ryml::Tree tree;
    {
        py::gil_scoped_release nogil;
        ensure_throwing_callbacks();
        const std::string bytes = f.read_bytes();
        try {
            tree = ryml::parse_in_arena(ryml::csubstr(bytes.data(), bytes.size()));
        } catch (const std::runtime_error& e) {
            throw std::runtime_error(std::string(e.what()) + " in " + f.fspath());
        }
        tree.resolve();                             // expand anchors / *aliases
    }
    return node_to_py(tree.crootref(), keys);
}

// ── JSON engine (simdjson) ─────────────────────────────────────────────────
// Recursively materialise a simdjson DOM element as a native Python object.
// JSON semantics are exact: object keys are strings, numbers arrive already
// typed by the parser (int64 / uint64 / double).
[[nodiscard]] inline py::object json_to_py(simdjson::dom::element el, KeyCache& keys) {
    using simdjson::dom::element_type;
    switch (el.type()) {
        case element_type::OBJECT: {
            py::dict out;
            for (auto [key, value] : simdjson::dom::object(el)) {
                out[keys.get(key)] = json_to_py(value, keys);
            }
            return out;
        }
        case element_type::ARRAY: {
            py::list out;
            for (simdjson::dom::element child : simdjson::dom::array(el)) {
                out.append(json_to_py(child, keys));
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

[[nodiscard]] inline py::object load_json(const file& f, KeyCache& keys) {
    simdjson::dom::parser parser;   // must outlive the element it returns
    simdjson::dom::element doc;
    {
        py::gil_scoped_release nogil;
        try {
            doc = parser.load(f.fspath());
        } catch (const simdjson::simdjson_error& e) {
            throw std::runtime_error("JSON parse error (" + f.fspath() + "): " + e.what());
        }
    }
    return json_to_py(doc, keys);
}

// ── TOML engine (toml++) ────────────────────────────────────────────────────
// Dates and times materialise as datetime.date / time / datetime — matching
// what the stdlib's tomllib produces, so the two are drop-in comparable.

[[nodiscard]] inline py::object toml_date_to_py(const toml::date& d) {
    static py::object cls = py::module_::import("datetime").attr("date");
    return cls(d.year, d.month, d.day);
}

[[nodiscard]] inline py::object toml_time_to_py(const toml::time& t) {
    static py::object cls = py::module_::import("datetime").attr("time");
    return cls(t.hour, t.minute, t.second, t.nanosecond / 1000);
}

[[nodiscard]] inline py::object toml_datetime_to_py(const toml::date_time& dt) {
    static py::object dt_cls = py::module_::import("datetime").attr("datetime");
    static py::object tz_cls = py::module_::import("datetime").attr("timezone");
    static py::object td_cls = py::module_::import("datetime").attr("timedelta");
    py::object tz = py::none();
    if (dt.offset) {
        tz = tz_cls(td_cls(py::arg("minutes") = dt.offset->minutes));
    }
    return dt_cls(dt.date.year, dt.date.month, dt.date.day, dt.time.hour, dt.time.minute,
                  dt.time.second, dt.time.nanosecond / 1000, tz);
}

[[nodiscard]] inline py::object toml_to_py(const toml::node& n, KeyCache& keys) {
    if (const toml::table* t = n.as_table()) {
        py::dict out;
        for (const auto& [key, value] : *t) out[keys.get(key.str())] = toml_to_py(value, keys);
        return out;
    }
    if (const toml::array* a = n.as_array()) {
        py::list out;
        for (const toml::node& child : *a) out.append(toml_to_py(child, keys));
        return out;
    }
    if (const auto* v = n.as_string())         return py::str(v->get());
    if (const auto* v = n.as_integer())        return py::int_(v->get());
    if (const auto* v = n.as_floating_point()) return py::float_(v->get());
    if (const auto* v = n.as_boolean())        return py::bool_(v->get());
    if (const auto* v = n.as_date())           return toml_date_to_py(v->get());
    if (const auto* v = n.as_time())           return toml_time_to_py(v->get());
    if (const auto* v = n.as_date_time())      return toml_datetime_to_py(v->get());
    throw std::runtime_error("toml: unhandled node type");
}

[[nodiscard]] inline py::object load_toml(const file& f, KeyCache& keys) {
    toml::parse_result result = [&f] {
        py::gil_scoped_release nogil;
        const std::string bytes = f.read_bytes();
        return toml::parse(std::string_view(bytes), std::string_view(f.fspath()));
    }();
    if (!result) {
        const auto& err = result.error();
        throw std::runtime_error("TOML parse error (" + f.fspath() + ", line " +
                                 std::to_string(err.source().begin.line) +
                                 "): " + std::string(err.description()));
    }
    return toml_to_py(result.table(), keys);
}

// ── Write side: Python object -> ryml tree -> YAML / JSON text ─────────────
// Strings are double-quoted exactly when an unquoted spelling would read back
// typed (scalar_is_string() is false) — the same constexpr classifiers that
// gate reading also guarantee the round-trip.

// Serialise a scalar into the tree arena, returning the stored csubstr.
[[nodiscard]] inline ryml::csubstr arena_sv(ryml::Tree& tree, std::string_view s) {
    return tree.to_arena(ryml::csubstr(s.data(), s.size()));
}

inline void py_to_node(ryml::Tree& tree, ryml::NodeRef node, py::handle obj, bool json_mode) {
    auto set_scalar = [&](std::string_view text, bool quote) {
        node.set_val(arena_sv(tree, text));
        if (quote) node |= ryml::VAL_DQUO;
    };

    if (obj.is_none()) {
        node.set_val("null");
        return;
    }
    if (py::isinstance<py::bool_>(obj)) {   // before int: bool subclasses int
        node.set_val(obj.cast<bool>() ? "true" : "false");
        return;
    }
    if (py::isinstance<py::int_>(obj)) {
        set_scalar(py::str(obj).cast<std::string>(), false);
        return;
    }
    if (py::isinstance<py::float_>(obj)) {
        const double d = obj.cast<double>();
        if (std::isinf(d) || std::isnan(d)) {
            if (json_mode) {
                throw std::invalid_argument("json cannot represent non-finite floats");
            }
            node.set_val(std::isnan(d) ? ryml::csubstr(".nan") :
                         d > 0 ? ryml::csubstr(".inf") : ryml::csubstr("-.inf"));
            return;
        }
        set_scalar(py::repr(obj).cast<std::string>(), false);
        return;
    }
    if (py::isinstance<py::str>(obj)) {
        const std::string s = obj.cast<std::string>();
        set_scalar(s, json_mode || !detail::scalar_is_string(s) || s.empty());
        return;
    }
    if (py::isinstance<py::dict>(obj)) {
        node |= ryml::MAP;
        for (auto item : obj.cast<py::dict>()) {
            if (!py::isinstance<py::str>(item.first)) {
                throw std::invalid_argument("write: mapping keys must be str, got " +
                                            py::str(py::type::of(item.first)).cast<std::string>());
            }
            const std::string k = item.first.cast<std::string>();
            ryml::NodeRef child = node.append_child();
            child.set_key(arena_sv(tree, k));
            if (json_mode || !detail::scalar_is_string(k) || k.empty()) {
                child |= ryml::KEY_DQUO;
            }
            py_to_node(tree, child, item.second, json_mode);
        }
        return;
    }
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        node |= ryml::SEQ;
        for (auto item : obj.cast<py::sequence>()) {
            ryml::NodeRef child = node.append_child();
            py_to_node(tree, child, item, json_mode);
        }
        return;
    }
    throw std::invalid_argument("write: unsupported type " +
                                py::str(py::type::of(obj)).cast<std::string>());
}

}  // namespace detail

// Read `f` and decode it with `engine`, all native C++: YAML via rapidyaml,
// JSON via simdjson (SIMD-accelerated), TOML via toml++. `key_cache_capacity`
// bounds the per-read key-interning cache (0 disables it).
[[nodiscard]] inline py::object load(const file& f, Engine engine,
                                     std::size_t key_cache_capacity = 256) {
    detail::KeyCache keys(key_cache_capacity);
    switch (engine) {
        case Engine::Yaml: return detail::load_yaml(f, keys);
        case Engine::Json: return detail::load_json(f, keys);
        case Engine::Toml: return detail::load_toml(f, keys);
        case Engine::Unknown: break;
    }
    throw std::invalid_argument("no engine resolved for " + f.fspath());
}

// Serialise `obj` to `f` with `engine`. The tree is built under the GIL (it
// reads Python objects); emit + file write run with the GIL released.
inline void write(const file& f, py::handle obj, Engine engine) {
    if (engine == Engine::Toml) {
        throw std::invalid_argument("toml write not implemented yet (read-only engine)");
    }
    if (engine == Engine::Unknown) {
        throw std::invalid_argument("no engine resolved for " + f.fspath());
    }
    const bool json_mode = engine == Engine::Json;
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    detail::py_to_node(tree, root, obj, json_mode);
    {
        py::gil_scoped_release nogil;
        std::string text;
        if (json_mode) {
            ryml::emitrs_json(tree, tree.root_id(), &text);
        } else {
            ryml::emitrs_yaml(tree, tree.root_id(), &text);
        }
        std::ofstream ofs(f.path(), std::ios::binary | std::ios::trunc);
        if (!ofs) throw std::runtime_error("cannot open file for writing: " + f.fspath());
        ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!ofs) throw std::runtime_error("write failed: " + f.fspath());
    }
}

}  // namespace pygim::pathlike
