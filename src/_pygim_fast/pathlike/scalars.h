#pragma once
// pathlike/scalars.h — the YAML 1.2 core-schema scalar rules, compile-time proven.
//
// CORE layer: pybind-free, like core.h. The constexpr classifiers decide WHAT
// a scalar is (null / bool / int / float / string); they gate reading and
// drive the write side's quoting. Materialising VALUES from these decisions
// is Python work and lives in adapter/materialize.h.

#include <limits>
#include <string>
#include <string_view>

namespace pygim::pathlike {

namespace detail {

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

// True when an unquoted scalar resolves to a plain string (i.e. none of the
// core-schema classifiers claim it). Also the emit-side quoting rule: a
// string value that is NOT plain must be quoted, or it would read back typed.
[[nodiscard]] constexpr bool scalar_is_string(std::string_view s) noexcept {
    bool b = false;
    double d = 0.0;
    return !is_null_scalar(s) && !parse_bool(s, b) && !is_core_int(s) &&
           !is_core_float(s) && !parse_special_float(s, d);
}

// Spot proof only — the exhaustive suites (keyword-triple consistency, every
// accepted and rejected spelling per classifier) live in
// tests/static/pathlike_scalar_proofs.cpp, compiled by every build.
static_assert(is_core_int("0x1A") && !is_core_int("1_000"));
static_assert(is_core_float("1e3") && !is_core_float("inf"));
static_assert(scalar_is_string("yes") && !scalar_is_string("true"));

}  // namespace detail
}  // namespace pygim::pathlike
