// Compile-time proofs for pathlike/scalars.h — the YAML 1.2 core-schema
// scalar rules, exhaustively. Wired into the extension build via
// ext.pathlike.toml so every build proves them (see pathlike_core_proofs.cpp
// for why that wiring matters). Contributes no runtime code.

#include "../../src/_pygim_fast/pathlike/scalars.h"

namespace {

using namespace pygim::pathlike::detail;

// ── The keyword triples hang together ──────────────────────────────────────
// lower is all lower-case, upper is its exact upper-casing, and the middle
// form is a distinct re-casing of the same word. (The middle form is not
// uniformly Title-case — the schema says Inf but NaN — so its exact spelling
// is pinned by the behaviour asserts below instead.)
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

// ── Behaviour proofs: exact spellings and no others ────────────────────────
// YAML 1.1 forms (yes/no/on/off, y/n, underscored ints) deliberately stay strings.

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

// ── Integer forms: [-+]?[0-9]+, 0x hex, 0o octal ───────────────────────────
static_assert(is_core_int("0") && is_core_int("-12") && is_core_int("+12") &&
              is_core_int("010") && is_core_int("12345678901234567890123"));
static_assert(is_core_int("0x1A") && is_core_int("0xff") && is_core_int("0o17"));
static_assert(!is_core_int("") && !is_core_int("-") && !is_core_int("0x") &&
              !is_core_int("0o") && !is_core_int("0o8") && !is_core_int("0xGG"));
static_assert(!is_core_int("1.5") && !is_core_int("1e3") && !is_core_int("0b1") &&
              !is_core_int("1_000") && !is_core_int("-0x1A"));   // 1.1-isms stay strings

// ── Float form: [-+]? ( \.[0-9]+ | [0-9]+(\.[0-9]*)? ) ( [eE][-+]?[0-9]+ )? ─
static_assert(is_core_float("2.5") && is_core_float("-.5") && is_core_float(".5") &&
              is_core_float("5.") && is_core_float("0.") && is_core_float("+0.5e+2"));
static_assert(is_core_float("1e3") && is_core_float("-1E-3") && is_core_float("1.5e10"));
static_assert(!is_core_float("") && !is_core_float(".") && !is_core_float("-") &&
              !is_core_float("e3") && !is_core_float(".e3") && !is_core_float("1e") &&
              !is_core_float("1e+") && !is_core_float("1.5.2") && !is_core_float("1 "));
static_assert(!is_core_float("inf") && !is_core_float("nan") && !is_core_float("infinity") &&
              !is_core_float("INF") && !is_core_float("0x1A"));   // strtod-isms stay strings

// ── The umbrella: what is a plain string ───────────────────────────────────
static_assert(scalar_is_string("hello") && scalar_is_string("yes") &&
              scalar_is_string("0b1") && scalar_is_string("inf"));
static_assert(!scalar_is_string("null") && !scalar_is_string("true") &&
              !scalar_is_string("0x1A") && !scalar_is_string("2.5") &&
              !scalar_is_string(".inf") && !scalar_is_string(""));

[[maybe_unused]] constexpr bool kScalarProofsCompiled = true;

}  // namespace
