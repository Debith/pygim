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
#include <initializer_list>
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

// ── uri.h: RFC 3986 as constexpr values ────────────────────────────────────
// (Every probe returns a bool computed inside: the libstdc++ of GCC 13/14
// cannot constant-evaluate a short std::string that escapes into the
// assertion expression.)
namespace uri_proofs {
consteval bool parses(std::string_view text, std::string_view scheme, bool has_auth, std::string_view auth,
                      bool absolute, std::initializer_list<std::string_view> segs,
                      bool has_query = false, std::string_view query = {}, bool has_frag = false, std::string_view frag = {}) {
    const uri u = uri::parse(text);
    if (u.scheme != scheme || u.has_authority != has_auth || u.authority != auth || u.absolute != absolute) return false;
    if (u.has_query != has_query || u.query != query || u.has_fragment != has_frag || u.fragment != frag) return false;
    if (u.segments.size() != segs.size()) return false;
    std::size_t i = 0;
    for (std::string_view want : segs) { if (u.segments[i++] != want) return false; }
    return true;
}
consteval bool round_trips(std::string_view text) { const uri u = uri::parse(text); return u.render() == text; }
consteval bool renders(std::string_view text, std::string_view want) { const uri u = uri::parse(text); return u.render() == want; }
consteval bool normalizes(std::string_view text, std::string_view want) { const uri u = uri::parse(text); const uri n = u.normalized(); return n.render() == want; }
consteval bool encodes(std::string_view raw, std::string_view want) { return uri::percent_encode(raw) == want; }
consteval bool decodes(std::string_view enc, std::string_view want) { return uri::percent_decode(enc) == want; }
consteval bool path_is(std::string_view text, std::string_view want) { const uri u = uri::parse(text); return u.path() == want; }
}  // namespace uri_proofs
using namespace uri_proofs;

// Appendix B decomposition, with decoded segments and empties/dots kept as written.
static_assert(parses("file:///tmp/a%20b/x.yaml", "file", true, "", true, {"tmp", "a b", "x.yaml"}));
static_assert(parses("file://localhost/etc/hosts", "file", true, "localhost", true, {"etc", "hosts"}));
static_assert(parses("file://srv/share/x", "file", true, "srv", true, {"share", "x"}));
static_assert(parses("file:/tmp/x", "file", false, "", true, {"tmp", "x"}));                 // RFC 8089 minimal form
static_assert(parses("file:x.yaml", "file", false, "", false, {"x.yaml"}));                   // relative reference
static_assert(parses("s3://bucket/k?v=1#frag", "s3", true, "bucket", true, {"k"}, true, "v=1", true, "frag"));
static_assert(parses("/a//b/./c/../", "", false, "", true, {"a", "", "b", ".", "c", "..", ""}));   // nothing removed
static_assert(parses("", "", false, "", false, {}));
static_assert(parses("/", "", false, "", true, {}));
static_assert(parses("a:b", "a", false, "", false, {"b"}));                                   // "a" is a valid scheme
static_assert(parses("1a:b", "", false, "", false, {"1a:b"}));                                // schemes start with ALPHA
static_assert(path_is("file:///tmp/a%20b", "/tmp/a b") && path_is("file:x/y", "x/y"));

// §5.3 recomposition round-trips canonical text; §2.1 encoding is exactly the non-pchar set.
static_assert(round_trips("file:///tmp/a%20b/x.yaml") && round_trips("s3://bucket/k?v=1#frag") && round_trips("/a//b/") &&
              round_trips("file://srv/share/x"));
static_assert(renders("file:///a%2Fb", "file:///a%2Fb"));                                      // "/" inside a segment stays encoded
static_assert(encodes("a b/c#d?e", "a%20b%2Fc%23d%3Fe") && encodes("a:b@c!$&'()*+,;=-._~", "a:b@c!$&'()*+,;=-._~") &&
              encodes("\xC3\xA9", "%C3%A9"));
static_assert(decodes("a%20b%2Fc", "a b/c") && decodes("%C3%A9", "\xC3\xA9") && decodes("100%", "100%") &&
              decodes("%zz%4", "%zz%4"));                                                       // malformed escapes kept

// §6.2.2 normalisation is explicit: scheme and host fold, userinfo/port do not, dot-segments go.
static_assert(normalizes("HTTP://User@Example.COM:8080/a/./b/../c", "http://User@example.com:8080/a/c"));
static_assert(normalizes("file:///a/b/../", "file:///a/"));
static_assert(normalizes("file:///a/..", "file:///"));
static_assert(normalizes("x://[::1]:80/p", "x://[::1]:80/p"));

// ── strategies: native text <-> uri, the Windows rules provable on any host ─
// (Exhaustive pathlib parity lives in pathlike_parity_proofs.cpp, generated
// from pathlib itself; these pin the mechanics and the URI mapping.)
namespace strategy_proofs {
using px = basic_file<posix_strategy>;
using wx = basic_file<windows_strategy>;
consteval bool px_is(std::string_view text, std::string_view want) { px f(text); return f.fspath() == want; }
consteval bool wx_is(std::string_view text, std::string_view want) { wx f(text); return f.fspath() == want; }
consteval bool px_uri(std::string_view text, std::string_view want) { px f(text); return f.as_uri() == want; }
consteval bool wx_uri(std::string_view text, std::string_view want) { wx f(text); return f.as_uri() == want; }
consteval bool px_join(std::string_view a, std::string_view b, std::string_view want) { px f(a); px j = f.joined(b); return j.fspath() == want; }
consteval bool wx_join(std::string_view a, std::string_view b, std::string_view want) { wx f(a); wx j = f.joined(b); return j.fspath() == want; }
consteval bool px_problem(std::string_view text, std::string_view want) { return px::problem(text) == want; }
consteval bool px_repr(std::string_view text, std::string_view want) { px f(text); return f.repr() == want; }
consteval bool px_equal(std::string_view a, std::string_view b) { px f(a); px g(b); return f == g; }
consteval std::size_t px_parents(std::string_view text) { px f(text); return f.parents().size(); }
}  // namespace strategy_proofs
using namespace strategy_proofs;

// file:// URIs in (pathlib.Path.from_uri rules), RFC URIs out.
static_assert(px_is("file:///tmp/a%20b/x.yaml", "/tmp/a b/x.yaml") && px_is("file://localhost/etc/hosts", "/etc/hosts") &&
              px_is("FILE:///tmp/x", "/tmp/x") && px_is("file:/tmp/x", "/tmp/x") && px_is("file:///tmp//a/./b/", "/tmp/a/b") &&
              px_is("file://srv/share/x", "//srv/share/x"));
static_assert(wx_is("file:///C:/Users/x.yaml", "C:\\Users\\x.yaml") && wx_is("file:///C|/x", "C:\\x") &&
              wx_is("file://srv/share/x.toml", "\\\\srv\\share\\x.toml") && wx_is("file://localhost/C:/x", "C:\\x"));
static_assert(px_uri("/tmp/a b/x.yaml", "file:///tmp/a%20b/x.yaml") && px_uri("//srv/share/x", "file:////srv/share/x") &&
              px_uri("some.yaml", "file://some.yaml") && px_uri("a b/c", "file://a%20b/c"));
static_assert(wx_uri("C:\\a b\\c.json", "file:///C:/a%20b/c.json") && wx_uri("\\\\srv\\share\\x", "file://srv/share/x") &&
              wx_uri("C:x", "file://C:/x"));
static_assert(px_problem("file:x.yaml", "URI is not absolute: 'file:x.yaml'") && px_problem("file://", "URI is not absolute: 'file://'") &&
              px_problem("s3://b/x", "unsupported URI scheme 's3' in 's3://b/x' (only file:// URIs are accepted)") &&
              px_problem("note:x.yaml", "") && px_problem("/plain/path", "") && px_problem("file:///ok", ""));
static_assert(px_repr("x.dat", "file(\"file://x.dat\")"));

// pathlib's join rules per strategy.
static_assert(px_join("/a/b", "c", "/a/b/c") && px_join("/a/b", "/x", "/x") && px_join("a", "b/c/", "a/b/c") && px_join("", "x", "x"));
static_assert(wx_join("C:\\a", "b", "C:\\a\\b") && wx_join("C:\\a", "\\x", "C:\\x") && wx_join("C:\\a", "D:x", "D:x") &&
              wx_join("C:\\a", "C:x", "C:\\a\\x") && wx_join("C:\\a", "\\\\srv\\s\\y", "\\\\srv\\s\\y") && wx_join("a", "b", "a\\b"));

// Value equality is spelling-independent; parents stop at the anchor (the "." parent is not listed).
static_assert(px_equal("a/b/", "a//b") && px_equal("./a/b", "a/b") && !px_equal("a/b", "a/c") && px_equal("", "."));
static_assert(px_parents("a/b/c") == 2 && px_parents("/a/b") == 2 && px_parents("a") == 0 && px_parents("/") == 0);

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
