// Compile-time proofs for pathlike/core.h — the exhaustive suites.
//
// This TU is part of the pygim.pathlike extension's SOURCES (see
// ext.pathlike.toml): it is compiled by every build, so a violated invariant
// cannot produce a binary. It contributes no runtime code — a static test
// that nothing compiles is no test at all, which is why these are wired into
// the build rather than sitting inert. Short spot-asserts stay next to the
// definitions in core.h; everything bulky and exhaustive lives here.

#include "../../src/_pygim_fast/pathlike/core.h"

namespace {

using namespace pygim::pathlike;
using pygim::pathlike::detail::ascii_lower;
using pygim::pathlike::detail::glob_match;

// ── Exhaustive proof of the format registry ────────────────────────────────
// Spot checks in core.h pin known entries; these validators sweep the WHOLE
// table, so a format added to kExtEngines later is proven automatically.

// Every entry resolves to its declared engine, never Unknown, and keeps the
// key contract: leading dot, lower-case (ascii_lower() is a no-op on keys).
consteval bool table_entries_resolve() {
    for (const auto& [ext, eng] : kExtEngines) {
        if (eng == Engine::Unknown) return false;
        if (engine_for_ext(ext) != eng) return false;
        if (ext.size() < 2 || ext.front() != '.') return false;
        if (ascii_lower(ext) != ext) return false;
    }
    return true;
}

// Duplicate keys would make later entries silently unreachable (first match wins).
consteval bool table_has_no_duplicates() {
    for (std::size_t i = 0; i < kExtEngines.size(); ++i) {
        for (std::size_t j = i + 1; j < kExtEngines.size(); ++j) {
            if (kExtEngines[i].first == kExtEngines[j].first) return false;
        }
    }
    return true;
}

// engine_label() -> engine_from_name() round-trips for every reachable engine.
consteval bool labels_roundtrip() {
    for (const auto& [ext, eng] : kExtEngines) {
        if (engine_from_name(engine_label(eng)) != eng) return false;
    }
    return engine_label(Engine::Unknown) == std::string_view{"unknown"};
}

// The lower-case-then-lookup chain used by resolve_engine(), proven end to end.
consteval bool case_folds_before_lookup() {
    return engine_for_ext(ascii_lower(".YAML")) == Engine::Yaml &&
           engine_for_ext(ascii_lower(".Yml")) == Engine::Yaml &&
           engine_for_ext(ascii_lower(".JSON")) == Engine::Json;
}

// Near-misses stay Unknown: raw lookups are exact (case, dot, whole string).
consteval bool misses_stay_unknown() {
    return engine_for_ext("") == Engine::Unknown &&
           engine_for_ext(".") == Engine::Unknown &&
           engine_for_ext("yaml") == Engine::Unknown &&
           engine_for_ext(".yaml ") == Engine::Unknown &&
           engine_for_ext(".YAML") == Engine::Unknown &&
           engine_from_name("YAML") == Engine::Unknown &&
           engine_from_name(".yaml") == Engine::Unknown;
}

static_assert(table_entries_resolve());
static_assert(table_has_no_duplicates());
static_assert(labels_roundtrip());
static_assert(case_folds_before_lookup());
static_assert(misses_stay_unknown());
// Out-of-range enum values (reachable via static_cast) still label as "unknown".
static_assert(engine_label(static_cast<Engine>(42)) == std::string_view{"unknown"});

// ── glob segment matcher ───────────────────────────────────────────────────
static_assert(glob_match("*", "anything") && glob_match("*.yaml", "a.yaml") &&
              glob_match("a?c", "abc") && glob_match("a*c*e", "abcde") &&
              glob_match("*.tar.*", "x.tar.gz") && glob_match("**", "name"));
static_assert(!glob_match("*.yaml", "a.yml") && !glob_match("a?c", "ac") &&
              !glob_match("", "x") && !glob_match("b*", "abc") && glob_match("", ""));

[[maybe_unused]] constexpr bool kCoreProofsCompiled = true;

}  // namespace
