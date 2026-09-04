// Compile-time proofs for pathlike/core.h and pathlike/engine_list.h — the
// exhaustive suites, on SYNTHETIC packs.
//
// This TU is part of the pygim.pathlike extension's SOURCES (see
// ext.pathlike.toml): it is compiled by every build, so a violated invariant
// cannot produce a binary. It contributes no runtime code — a static test
// that nothing compiles is no test at all, which is why these are wired into
// the build rather than sitting inert.
//
// The registry is generic over its pack, so it is proven here on small
// pybind-free descriptors — POSITIVELY (a good pack satisfies every predicate
// and every lookup answers correctly) and NEGATIVELY (each broken pack is
// caught by exactly the predicate that should catch it, with the report
// naming the offender). The REAL pack the build assembled is proven in
// bindings.cpp with the same predicates (static_assert(Engines::holds())),
// which is what makes a new engine header self-verifying on its first build.

#include "../../src/_pygim_fast/pathlike/engine_list.h"

#include <array>
#include <string_view>

namespace {

using namespace pygim::pathlike;
using pygim::pathlike::detail::ascii_lower;
using pygim::pathlike::detail::ascii_upper;
using pygim::pathlike::detail::glob_match;

// ── Synthetic descriptors (metadata only: the registry never needs load/write) ──
struct alpha {
    static constexpr std::array<std::string_view, 2> exts{".a", ".aa"};
    static constexpr std::array<std::string_view, 1> aliases{"al"};
    static constexpr engine_info info{.name = "alpha", .label = "liba", .doc = "Alpha format.",
                                      .exts = exts, .aliases = aliases};
};
struct beta {
    static constexpr std::array<std::string_view, 1> exts{".b"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "beta", .label = "lib-b", .doc = "Beta format.",
                                      .exts = exts, .aliases = aliases};
};
static_assert(EngineMeta<alpha> && EngineMeta<beta>);
static_assert(!EngineMeta<int>);

using Good = engine_list<alpha, beta>;

// ── Positive proofs ────────────────────────────────────────────────────────
static_assert(Good::holds());
static_assert(Good::size == 2);
static_assert(Good::for_ext(".a") == &alpha::info && Good::for_ext(".aa") == &alpha::info &&
              Good::for_ext(".b") == &beta::info && Good::for_ext(".c") == nullptr);
static_assert(Good::from_name("alpha") == &alpha::info && Good::from_name("liba") == &alpha::info &&
              Good::from_name("al") == &alpha::info && Good::from_name("beta") == &beta::info &&
              Good::from_name("lib-b") == &beta::info && Good::from_name("xml") == nullptr);
static_assert(Good::index_of(&alpha::info) == 0 && Good::index_of(&beta::info) == 1 &&
              Good::index_of(nullptr) == Good::size);
static_assert(Good::id_of<alpha>() == 0 && Good::id_of<beta>() == 1);
static_assert(Good::known == "alpha/liba, beta/lib-b");
static_assert(Good::ext_inventory == ".a .aa .b");
static_assert(class_name<alpha> == "alphafile" && class_name<beta> == "betafile");
static_assert(detail::class_name_buf<alpha>[9] == '\0');   // NUL-terminated for pybind11
static_assert(Good::visit(1, []<class E>() { return E::info.name; }) == "beta");
static_assert(Good::conflict_report().view() == "pathlike engine registry: all invariants hold");

// visit() with a void visitor, and for_each(): the pack iteration primitives.
consteval std::size_t visit_void_hits() {
    std::size_t hits = 0;
    Good::visit(0, [&]<class E>() { hits += E::info.exts.size(); });
    return hits;
}
static_assert(visit_void_hits() == 2);

consteval std::size_t total_exts() {
    std::size_t n = 0;
    Good::for_each([&]<class E>() { n += E::info.exts.size(); });
    return n;
}
static_assert(total_exts() == 3);

// The empty pack is a valid (if useless) registry: bindings.cpp rejects it separately.
static_assert(engine_list<>::holds() && engine_list<>::size == 0 && engine_list<>::known.empty() &&
              engine_list<>::ext_inventory.empty() && engine_list<>::for_ext(".a") == nullptr);

// ── Negative proofs: each broken pack is caught, and the report names it ───
struct dup_ext {   // claims alpha's ".a"
    static constexpr std::array<std::string_view, 1> exts{".a"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "dupe", .label = "libd", .doc = "d", .exts = exts, .aliases = aliases};
};
static_assert(engine_list<alpha, dup_ext>::names_wellformed() && engine_list<alpha, dup_ext>::exts_wellformed());
static_assert(!engine_list<alpha, dup_ext>::no_duplicate_exts() && !engine_list<alpha, dup_ext>::holds());
static_assert(engine_list<alpha, dup_ext>::conflict_report().view() == "'.a' is claimed by both 'alpha' and 'dupe'");

struct self_dup_ext {   // lists its own extension twice
    static constexpr std::array<std::string_view, 2> exts{".s", ".s"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "selfdup", .label = "libs", .doc = "s", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<self_dup_ext>::no_duplicate_exts());

struct upper_ext {   // ".B" can never be produced by ext_key()
    static constexpr std::array<std::string_view, 1> exts{".B"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "upper", .label = "libu", .doc = "u", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<upper_ext>::exts_wellformed() && !engine_list<upper_ext>::holds());
static_assert(engine_list<upper_ext>::conflict_report().view() ==
              "engine 'upper': extension '.B' must be lower-case with one leading dot and no blanks");

struct two_dots {   // fs::path::extension() only ever yields the last part
    static constexpr std::array<std::string_view, 1> exts{".tar.gz"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "tarball", .label = "libt", .doc = "t", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<two_dots>::exts_wellformed());

struct no_dot {
    static constexpr std::array<std::string_view, 1> exts{"nd"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "nodot", .label = "libn", .doc = "n", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<no_dot>::exts_wellformed());

struct label_clash {   // alias "liba" is alpha's label
    static constexpr std::array<std::string_view, 1> exts{".c"};
    static constexpr std::array<std::string_view, 1> aliases{"liba"};
    static constexpr engine_info info{.name = "clash", .label = "libc", .doc = "c", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<alpha, label_clash>::no_duplicate_selectors() && !engine_list<alpha, label_clash>::holds());
static_assert(engine_list<alpha, label_clash>::conflict_report().view() ==
              "selector 'liba' is claimed by both 'alpha' and 'clash'");

struct name_clash {   // same format name as alpha
    static constexpr std::array<std::string_view, 1> exts{".d"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "alpha", .label = "libx", .doc = "x", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<alpha, name_clash>::no_duplicate_selectors());

struct bad_name {   // would be the Python class "Bad-Namefile"
    static constexpr std::array<std::string_view, 1> exts{".e"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "Bad-Name", .label = "libe", .doc = "e", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<bad_name>::names_wellformed() && !engine_list<bad_name>::holds());
static_assert(engine_list<bad_name>::conflict_report().view() ==
              "engine name 'Bad-Name' must match [a-z][a-z0-9_]* (it becomes the Python class '<name>file')");

struct self_alias {   // repeats its own name as an alias
    static constexpr std::array<std::string_view, 1> exts{".f"};
    static constexpr std::array<std::string_view, 1> aliases{"selfish"};
    static constexpr engine_info info{.name = "selfish", .label = "libf", .doc = "f", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<self_alias>::no_duplicate_selectors());

struct upper_alias {   // selectors are matched exactly, so "AL" could never be typed to match
    static constexpr std::array<std::string_view, 1> exts{".g"};
    static constexpr std::array<std::string_view, 1> aliases{"AL"};
    static constexpr engine_info info{.name = "upperalias", .label = "libg", .doc = "g", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<upper_alias>::names_wellformed());

struct no_doc {
    static constexpr std::array<std::string_view, 1> exts{".h"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "nodoc", .label = "libh", .doc = "", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<no_doc>::names_wellformed());

struct no_exts {
    static constexpr std::array<std::string_view, 0> exts{};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "noexts", .label = "libi", .doc = "i", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<no_exts>::names_wellformed());

struct label_is_name {   // name == label leaves nothing for .engine to distinguish
    static constexpr std::array<std::string_view, 1> exts{".j"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "same", .label = "same", .doc = "j", .exts = exts, .aliases = aliases};
};
static_assert(!engine_list<label_is_name>::no_duplicate_selectors());

// ── The case-fold chain, generated from the table ──────────────────────────
static_assert(Good::case_folding_is_exact());
static_assert(Good::for_ext(ascii_lower(".AA")) == &alpha::info && Good::for_ext(".AA") == nullptr &&
              Good::for_ext("aa") == nullptr && Good::for_ext(".aa ") == nullptr &&
              Good::from_name("ALPHA") == nullptr && Good::from_name(".alpha") == nullptr);

// ── core.h helpers ─────────────────────────────────────────────────────────
static_assert(ascii_lower(".YAML") == ".yaml" && ascii_upper(".yaml") == ".YAML" && ascii_lower("") == "");
static_assert(sv_list{}.empty() && !alpha::info.exts.empty() && alpha::info.exts.size() == 2 &&
              alpha::info.owns(".aa") && !alpha::info.owns(".b") && alpha::info.selects("al") &&
              !alpha::info.selects("beta"));

// glob segment matcher
static_assert(glob_match("*", "anything") && glob_match("*.yaml", "a.yaml") &&
              glob_match("a?c", "abc") && glob_match("a*c*e", "abcde") &&
              glob_match("*.tar.*", "x.tar.gz") && glob_match("**", "name"));
static_assert(!glob_match("*.yaml", "a.yml") && !glob_match("a?c", "ac") &&
              !glob_match("", "x") && !glob_match("b*", "abc") && glob_match("", ""));

#if PYGIM_PATHLIKE_REFLECTION
// ── Reflection helpers (P2996), on the synthetic descriptors ───────────────
static_assert(names_match_identifiers(engine_list<alpha, beta>{}));     // "alpha" is struct alpha
struct misnamed {
    static constexpr std::array<std::string_view, 1> exts{".m"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{.name = "other", .label = "libm", .doc = "m", .exts = exts, .aliases = aliases};
};
static_assert(!names_match_identifiers(engine_list<misnamed>{}));      // caught: name != identifier
static_assert(engine_list<>::size == 0 && names_match_identifiers(engine_list<>{}));
#endif

[[maybe_unused]] constexpr bool kCoreProofsCompiled = true;

}  // namespace
