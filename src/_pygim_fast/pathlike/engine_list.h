#pragma once
// pathlike/engine_list.h — the OPEN engine registry: a compile-time type list.
//
// Nobody edits a table to add a format. The registry IS the directory
// adapter/engines/: every header there declares one engine — a struct with a
// `static constexpr engine_info info` (name, label, doc, extensions, aliases)
// plus static load()/write() — and the build discovers the directory. setup.py
// reads `[extension.typelist]` from ext.pathlike.toml, globs the headers, and
// writes ONE generated header (build/gen/pathlike/pathlike_engines.gen.h):
//
//     #include "adapter/engines/json.h"        // one per file, sorted by stem
//     ...
//     namespace pygim::pathlike { using Engines = engine_list<engines::json, ...>; }
//
// Everything downstream is a fold over that pack: the extension and selector
// tables (static registries — wiring/registry/core.h over the mapping toolkit's
// flat_storage, the same RegistryCore the run-time Registry uses),
// the error-message inventories, the Python typed classes and their
// docstrings, the module's ENGINES record, and the proofs. Adding a format is
// adding a file; nothing in this header, adapter.h or bindings.cpp changes,
// and the new engine is swept by every proof on its first build.
//
// Why a generated header AND reflection: the build's glob is the portable
// source of the pack — it works on every compiler in the matrix (GCC 13 and
// Apple clang on CI degrade the c++26 flag to c++23). Where P2996 static
// reflection is available (GCC 16 with -freflection, which setup.py enables
// whenever the compiler accepts it), the compiler independently enumerates the
// engine structs of namespace `engines` (reflected_engines_t below) and
// bindings.cpp proves the two agree, plus that every engine's format name is
// its struct's identifier. Reflection cannot replace the glob outright — the
// engine headers still have to be #included from somewhere, and C++ has no
// directory include — so it is used for what it is uniquely good at: proving
// the generated list complete. Everything else here is portable C++23; the
// remaining C++26 feature (a computed static_assert message naming the
// offending engine, P2741) is gated on __cpp_static_assert.
//
// This header is pybind-free: it knows engines only through engine_info, so
// the proof TU (tests/static/pathlike_core_proofs.cpp) exercises the registry
// on synthetic packs — positively AND negatively — without any parser or
// Python header. adapter/adapter.h adds the load/write half of the contract.

#include <array>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "core.h"
#include "../wiring/registry/core.h"   // StaticRegistryCore: the tables below are registries, not hand-rolled scans

// P2996 detection: the standard feature-test macro is __cpp_reflection, but
// GCC 16 (the first implementation, behind -freflection) predefines
// __cpp_impl_reflection instead, so both spellings are honoured. A compiler
// that defines either ships <meta>, so the header is not probed for
// (repository rule: code must not vary with what happens to be installed —
// a feature macro is the compiler's own statement, not an environment probe).
#if (defined(__cpp_reflection) && __cpp_reflection >= 202506L) || \
    (defined(__cpp_impl_reflection) && __cpp_impl_reflection >= 202506L)
#define PYGIM_PATHLIKE_REFLECTION 1
#else
#define PYGIM_PATHLIKE_REFLECTION 0
#endif
#if PYGIM_PATHLIKE_REFLECTION
#include <algorithm>
#include <meta>
#include <vector>
#endif

namespace pygim::pathlike {

namespace engines {}   // the engine headers populate this; pre-declared so reflection can name it

// The pybind-free half of the engine contract: a descriptor exposes its
// engine_info. (adapter.h's `Engine` concept adds load()/write().)
template <class E>
concept EngineMeta = requires {
    { E::info } -> std::convertible_to<const engine_info&>;
};

namespace detail {

// A fixed-capacity constexpr text: used for the C++26 static_assert message
// (P2741 needs `.data()` and `.size()` usable in constant expressions) and for
// the inventories below. Truncates silently at capacity; every use is bounded.
struct report {
    std::array<char, 240> buf{};
    std::size_t len = 0;

    constexpr void put(std::string_view s) {
        for (char c : s) {
            if (len + 1 < buf.size()) buf[len++] = c;
        }
    }
    [[nodiscard]] constexpr const char* data() const noexcept { return buf.data(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return len; }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return {buf.data(), len}; }
};

// "json/simdjson, jsonl/simdjson-ndjson, ..." — the engine= inventory quoted by
// every "unknown engine" error. Sized by one fold, filled by another, at
// compile time; namespace-scope variable templates so engine_list can
// initialise its members from them without in-class ordering hazards.
// A cursor into a fixed buffer: the fold helpers below append through it.
// (Plain function templates rather than lambdas inside fold expressions —
// the portable subset every compiler in the matrix handles.)
struct cursor {
    char* at;
    std::size_t words = 0;   // items written so far (for separators)
    constexpr void put(std::string_view s) { for (char c : s) *at++ = c; }
};

template <EngineMeta E>
consteval std::size_t known_length_of(bool first) {
    return (first ? 0 : 2) + E::info.name.size() + 1 + E::info.label.size();
}

template <EngineMeta E>
constexpr void put_known(cursor& c) {
    if (c.words++) c.put(", ");
    c.put(E::info.name);
    c.put("/");
    c.put(E::info.label);
}

template <EngineMeta... Es>
consteval std::size_t known_length() {
    std::size_t n = 0, i = 0;
    ((n += known_length_of<Es>(i++ == 0)), ...);
    return n;
}

template <EngineMeta... Es>
consteval auto known_buffer() {
    std::array<char, known_length<Es...>() + 1> out{};
    [[maybe_unused]] cursor c{out.data()};
    (put_known<Es>(c), ...);
    return out;
}

template <EngineMeta... Es>
inline constexpr auto known_buf = known_buffer<Es...>();

// ".json .jsonl .ndjson .toml .yaml .yml" — the extension inventory quoted by
// every "no engine for extension" error.
template <EngineMeta E>
consteval std::size_t ext_inventory_length_of() {
    std::size_t n = 0;
    for (std::string_view x : E::info.exts) n += 1 + x.size();   // one separator per item
    return n;
}

template <EngineMeta E>
constexpr void put_exts(cursor& c) {
    for (std::string_view x : E::info.exts) {
        if (c.words++) c.put(" ");
        c.put(x);
    }
}

template <EngineMeta... Es>
consteval std::size_t ext_inventory_length() {
    std::size_t n = (ext_inventory_length_of<Es>() + ... + 0);
    return n ? n - 1 : 0;   // no separator before the first item
}

template <EngineMeta... Es>
consteval auto ext_inventory_buffer() {
    std::array<char, ext_inventory_length<Es...>() + 1> out{};
    [[maybe_unused]] cursor c{out.data()};
    (put_exts<Es>(c), ...);
    return out;
}

template <EngineMeta... Es>
inline constexpr auto ext_inventory_buf = ext_inventory_buffer<Es...>();

// Run-time counterparts of the inventories: plain std::string folds.
template <EngineMeta E>
void append_known_text(std::string& out) {
    if (!out.empty()) out += ", ";
    out += std::string(E::info.name) + "/" + std::string(E::info.label);
}
template <EngineMeta E>
void append_ext_text(std::string& out) {
    for (std::string_view x : E::info.exts) {
        if (!out.empty()) out += " ";
        out += std::string(x);
    }
}

// "<name>file" — the Python class name of an engine's typed file, as a
// NUL-terminated buffer with static storage (pybind11 may keep the pointer).
template <EngineMeta E>
inline constexpr auto class_name_buf = [] {
    std::array<char, E::info.name.size() + 5> out{};
    std::size_t p = 0;
    for (char c : E::info.name) out[p++] = c;
    for (char c : std::string_view{"file"}) out[p++] = c;
    return out;
}();

}  // namespace detail

// The Python class name of E's typed file: "jsonfile".
template <EngineMeta E>
inline constexpr std::string_view class_name{detail::class_name_buf<E>.data(), E::info.name.size() + 4};

// The registry, generic over the pack the build discovered.
template <EngineMeta... Es>
struct engine_list {
    static constexpr std::size_t size = sizeof...(Es);
    static constexpr std::array<const engine_info*, size> infos{&Es::info...};

    // Inventories for error messages and docstrings.
    static constexpr std::string_view known{detail::known_buf<Es...>.data(), detail::known_length<Es...>()};
    static constexpr std::string_view ext_inventory{detail::ext_inventory_buf<Es...>.data(),
                                                    detail::ext_inventory_length<Es...>()};

    // ── the tables: static registries (wiring/registry/core.h over flat_storage)
    // Both are built by one constexpr function from the pack. Registration is
    // strict, so a second engine claiming an extension or a selector throws —
    // which, when the table is built in constant evaluation, is a build error
    // (holds() below reports the conflict by name first, and short-circuits).
    // The same table object serves the proofs (built transiently in constant
    // evaluation) and the process (built once, on first use).
    using table = core::StaticRegistryCore<std::string_view, const engine_info*>;

    template <EngineMeta E>
    static constexpr void add_exts(table& t) {
        for (std::string_view x : E::info.exts) t.register_or_override(x, &E::info, false);
    }
    template <EngineMeta E>
    static constexpr void add_selectors(table& t) {
        t.register_or_override(E::info.name, &E::info, false);
        t.register_or_override(E::info.label, &E::info, false);
        for (std::string_view a : E::info.aliases) t.register_or_override(a, &E::info, false);
    }

    [[nodiscard]] static constexpr table ext_table() {
        table t;
        (add_exts<Es>(t), ...);
        return t;
    }

    [[nodiscard]] static constexpr table selector_table() {
        table t;
        (add_selectors<Es>(t), ...);
        return t;
    }

    // ── lookups ───────────────────────────────────────────────────────────
    // Two entry points per table, deliberately kept apart (no `if consteval`):
    // the constexpr ones build the table transiently and serve the proofs — a
    // constexpr std::vector cannot outlive its evaluation — and the run-time
    // ones build it once per process in a plain function. Both build it with
    // the same code, so the proven table IS the served table.
    [[nodiscard]] static constexpr const engine_info* lookup(const table& t, std::string_view key) noexcept {
        const engine_info* const* hit = t.try_get_const(key);
        return hit ? *hit : nullptr;
    }

    // The engine that auto-dispatches an extension (lower-case, leading dot), or nullptr.
    [[nodiscard]] static constexpr const engine_info* for_ext(std::string_view ext) {
        const table t = ext_table();
        return lookup(t, ext);
    }
    [[nodiscard]] static const engine_info* for_ext_at_runtime(std::string_view ext) {
        static const table t = ext_table();
        return lookup(t, ext);
    }

    // The engine an engine= selector names (format name, library label or alias), or nullptr.
    [[nodiscard]] static constexpr const engine_info* from_name(std::string_view n) {
        const table t = selector_table();
        return lookup(t, n);
    }
    [[nodiscard]] static const engine_info* from_name_at_runtime(std::string_view n) {
        static const table t = selector_table();
        return lookup(t, n);
    }

    // The inventories as run-time strings, built by plain folds (the constexpr
    // `known`/`ext_inventory` above serve the proofs; run-time code must not
    // odr-use a consteval-built variable template).
    [[nodiscard]] static std::string known_text() {
        std::string out;
        (detail::append_known_text<Es>(out), ...);
        return out;
    }
    [[nodiscard]] static std::string ext_inventory_text() {
        std::string out;
        (detail::append_ext_text<Es>(out), ...);
        return out;
    }

    // Position in the pack (== size when unknown).
    [[nodiscard]] static constexpr std::size_t index_of(const engine_info* e) noexcept {
        for (std::size_t i = 0; i < size; ++i) {
            if (infos[i] == e) return i;
        }
        return size;
    }

    template <EngineMeta E>
    [[nodiscard]] static constexpr std::size_t id_of() noexcept { return index_of(&E::info); }

    // ── pack iteration ────────────────────────────────────────────────────
    // f.template operator()<E>() for every engine, in pack order.
    template <class F>
    static constexpr void for_each(F&& f) {
        (f.template operator()<Es>(), ...);
    }

    // f.template operator()<E>() for the engine at position i (i < size).
    template <class F>
    static constexpr decltype(auto) visit(std::size_t i, F&& f) {
        return visit_impl(i, f, std::index_sequence_for<Es...>{});
    }

    // ── resolution (runtime; throws with derived inventories) ─────────────
    // The engine read()/write() would use for f: the constructor pin, else the extension.
    [[nodiscard]] static const engine_info* resolved(const file& f) {
        return f.pinned() ? f.pinned() : for_ext_at_runtime(f.ext_key());
    }

    // Precedence: an explicit engine= wins, then the pin, then the extension.
    // Throws std::invalid_argument (Python ValueError) rather than guessing.
    [[nodiscard]] static const engine_info* resolve(const file& f, std::string_view requested) {
        if (!requested.empty()) {
            if (const engine_info* e = from_name_at_runtime(requested)) return e;
            throw std::invalid_argument("unknown engine: '" + std::string(requested) + "' (known: " +
                                        known_text() + ")");
        }
        if (const engine_info* e = resolved(f)) return e;
        throw std::invalid_argument("no engine for extension '" + f.ext_key() + "' (known: " +
                                    ext_inventory_text() +
                                    ") — pass engine= at construction or read(engine=...)");
    }

    // ── proofs (constexpr predicates; every build asserts holds()) ────────
    // Written once, they sweep whatever pack the build assembled — a new engine
    // is proven on its first compile, and a broken one produces no binary.

    // [a-z][a-z0-9_]*: the name becomes the Python class "<name>file".
    [[nodiscard]] static constexpr bool ident_ok(std::string_view s) noexcept {
        if (s.empty() || !(s.front() >= 'a' && s.front() <= 'z')) return false;
        for (char c : s) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
        }
        return true;
    }

    // engine= selectors are matched exactly: no upper-case, no blanks, no leading dot.
    [[nodiscard]] static constexpr bool selector_ok(std::string_view s) noexcept {
        if (s.empty() || s.front() == '.') return false;
        for (char c : s) {
            if ((c >= 'A' && c <= 'Z') || c == ' ' || c == '\t') return false;
        }
        return true;
    }

    // ".yaml": leading dot, one dot only, lower-case, no blanks or separators —
    // exactly what ext_key() can produce, so ascii_lower is a no-op on keys.
    [[nodiscard]] static constexpr bool ext_ok(std::string_view x) noexcept {
        if (x.size() < 2 || x.front() != '.') return false;
        for (std::size_t i = 1; i < x.size(); ++i) {
            const char c = x[i];
            if (c == '.' || c == ' ' || c == '\t' || c == '/' || c == '\\' || (c >= 'A' && c <= 'Z')) return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr bool names_wellformed() noexcept {
        for (const engine_info* e : infos) {
            if (!ident_ok(e->name) || !selector_ok(e->label) || e->doc.empty() || e->exts.empty()) return false;
            for (std::string_view a : e->aliases) {
                if (!selector_ok(a)) return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool exts_wellformed() noexcept {
        for (const engine_info* e : infos) {
            for (std::string_view x : e->exts) {
                if (!ext_ok(x)) return false;
            }
        }
        return true;
    }

    // Every extension belongs to exactly one engine (and is listed once).
    [[nodiscard]] static constexpr bool no_duplicate_exts() noexcept {
        for (std::size_t i = 0; i < size; ++i) {
            for (std::size_t a = 0; a < infos[i]->exts.size(); ++a) {
                const std::string_view x = infos[i]->exts.begin()[a];
                for (std::size_t b = a + 1; b < infos[i]->exts.size(); ++b) {
                    if (infos[i]->exts.begin()[b] == x) return false;
                }
                for (std::size_t j = i + 1; j < size; ++j) {
                    if (infos[j]->owns(x)) return false;
                }
            }
        }
        return true;
    }

    // Every engine= selector belongs to exactly one engine — so from_name is
    // unambiguous — and an engine does not repeat its own name/label as an alias.
    [[nodiscard]] static constexpr bool no_duplicate_selectors() noexcept {
        for (std::size_t i = 0; i < size; ++i) {
            const engine_info* e = infos[i];
            if (e->name == e->label || e->aliases.contains(e->name) || e->aliases.contains(e->label)) return false;
            for (std::size_t a = 0; a < e->aliases.size(); ++a) {
                for (std::size_t b = a + 1; b < e->aliases.size(); ++b) {
                    if (e->aliases.begin()[a] == e->aliases.begin()[b]) return false;
                }
            }
            for (std::size_t j = i + 1; j < size; ++j) {
                const engine_info* o = infos[j];
                if (o->selects(e->name) || o->selects(e->label)) return false;
                for (std::string_view a : e->aliases) {
                    if (o->selects(a)) return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool every_ext_resolves() noexcept {
        for (const engine_info* e : infos) {
            for (std::string_view x : e->exts) {
                if (for_ext(x) != e) return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool every_selector_resolves() noexcept {
        for (const engine_info* e : infos) {
            if (from_name(e->name) != e || from_name(e->label) != e) return false;
            for (std::string_view a : e->aliases) {
                if (from_name(a) != e) return false;
            }
        }
        return true;
    }

    // The fold-then-lookup chain used at run time, and the near-misses that
    // must stay unknown — generated from the table, not hand-listed.
    [[nodiscard]] static constexpr bool case_folding_is_exact() {
        for (const engine_info* e : infos) {
            for (std::string_view x : e->exts) {
                if (for_ext(detail::ascii_lower(detail::ascii_upper(x))) != e) return false;
                if (detail::ascii_upper(x) != x && for_ext(detail::ascii_upper(x)) != nullptr) return false;
                if (for_ext(x.substr(1)) != nullptr) return false;                 // "yaml"
                if (for_ext(std::string(x) + " ") != nullptr) return false;        // ".yaml "
            }
            if (detail::ascii_upper(e->name) != e->name && from_name(detail::ascii_upper(e->name)) != nullptr) return false;
            if (from_name("." + std::string(e->name)) != nullptr) return false;   // ".yaml" is not a selector
        }
        return for_ext("") == nullptr && for_ext(".") == nullptr && from_name("") == nullptr;
    }

    // Order matters: the well-formedness and duplicate predicates come first and
    // short-circuit, so a conflict is reported by conflict_report() by name
    // rather than surfacing as a failed constant evaluation of ext_table().
    [[nodiscard]] static constexpr bool holds() {
        return names_wellformed() && exts_wellformed() && no_duplicate_exts() && no_duplicate_selectors() &&
               every_ext_resolves() && every_selector_resolves() && case_folding_is_exact();
    }

    // The first violated invariant, named: "'.json' is claimed by both 'json'
    // and 'csv'". Under C++26 this IS the static_assert message; older
    // dialects get a fixed message and the same proof.
    [[nodiscard]] static constexpr detail::report conflict_report() {
        detail::report r;
        if (holds()) {
            r.put("pathlike engine registry: all invariants hold");
            return r;
        }
        for (const engine_info* e : infos) {
            if (!ident_ok(e->name)) {
                r.put("engine name '"); r.put(e->name); r.put("' must match [a-z][a-z0-9_]* (it becomes the Python class '<name>file')");
                return r;
            }
            if (!selector_ok(e->label) || e->doc.empty() || e->exts.empty()) {
                r.put("engine '"); r.put(e->name); r.put("': label must be lower-case with no blanks, doc must be non-empty, exts must be non-empty");
                return r;
            }
            for (std::string_view a : e->aliases) {
                if (!selector_ok(a)) {
                    r.put("engine '"); r.put(e->name); r.put("': alias '"); r.put(a); r.put("' must be lower-case with no blanks or leading dot");
                    return r;
                }
            }
            for (std::string_view x : e->exts) {
                if (!ext_ok(x)) {
                    r.put("engine '"); r.put(e->name); r.put("': extension '"); r.put(x); r.put("' must be lower-case with one leading dot and no blanks");
                    return r;
                }
            }
        }
        for (std::size_t i = 0; i < size; ++i) {
            for (std::string_view x : infos[i]->exts) {
                for (std::size_t j = i + 1; j < size; ++j) {
                    if (infos[j]->owns(x)) {
                        r.put("'"); r.put(x); r.put("' is claimed by both '"); r.put(infos[i]->name); r.put("' and '"); r.put(infos[j]->name); r.put("'");
                        return r;
                    }
                }
            }
        }
        for (std::size_t i = 0; i < size; ++i) {
            const engine_info* e = infos[i];
            for (std::size_t j = i + 1; j < size; ++j) {
                const engine_info* o = infos[j];
                std::string_view clash{};
                if (o->selects(e->name)) clash = e->name;
                else if (o->selects(e->label)) clash = e->label;
                else for (std::string_view a : e->aliases) { if (o->selects(a)) { clash = a; break; } }
                if (!clash.empty()) {
                    r.put("selector '"); r.put(clash); r.put("' is claimed by both '"); r.put(e->name); r.put("' and '"); r.put(o->name); r.put("'");
                    return r;
                }
            }
        }
        r.put("pathlike engine registry invariants violated (duplicate alias within one engine, or a name/label/alias clash)");
        return r;
    }

private:
    template <class F, std::size_t... Is>
    static constexpr auto visit_impl(std::size_t i, F& f, std::index_sequence<Is...>) {
        using R = std::common_type_t<decltype(f.template operator()<Es>())...>;
        if constexpr (std::is_void_v<R>) {
            ((i == Is ? (f.template operator()<Es>(), true) : false) || ...);
        } else {
            R out{};
            ((i == Is ? (out = f.template operator()<Es>(), true) : false) || ...);
            return out;
        }
    }
};

#if PYGIM_PATHLIKE_REFLECTION
// ── Reflection (P2996): the compiler's own view of the registry ────────────
namespace detail {
// Every class type declared in namespace `ns`, sorted by identifier — the
// order the build's glob uses (sorted file stems), so the two are comparable.
consteval std::vector<std::meta::info> engine_types_of(std::meta::info ns) {
    std::vector<std::meta::info> out;
    for (std::meta::info m : std::meta::members_of(ns, std::meta::access_context::unchecked())) {
        if (std::meta::is_type(m) && std::meta::is_class_type(m)) out.push_back(m);
    }
    std::ranges::sort(out, {}, [](std::meta::info m) { return std::meta::identifier_of(m); });
    return out;
}
}  // namespace detail

// engine_list<every engine struct in namespace engines>, as the compiler sees
// it at the point of instantiation (after the engine headers are included).
template <template <class...> class List>
using reflected_engines_t = [: std::meta::substitute(^^List, detail::engine_types_of(^^engines)) :];

// Every engine's format name is its struct's identifier — the convention the
// build relies on (struct name == file stem), proven rather than assumed.
template <EngineMeta... Es>
consteval bool names_match_identifiers(engine_list<Es...>) noexcept {
    return ((Es::info.name == std::meta::identifier_of(^^Es)) && ... && true);
}
#endif  // PYGIM_PATHLIKE_REFLECTION

}  // namespace pygim::pathlike
