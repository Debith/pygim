// Compile-time proofs for traits/core.h — Python's private-name rule (CPython's _Py_Mangle) and
// the move of a name and a qualified name from one owner class to another.
//
// This TU is part of the pygim.traits extension's sources (ext.traits.toml): every build compiles
// it, so a violated law cannot produce a binary. It contributes no runtime code.

#include "../../src/_pygim_fast/traits/core.h"

#include <array>
#include <string>
#include <string_view>

namespace {

using pygim::traits::is_private_name;
using pygim::traits::mangled;
using pygim::traits::owner_of;
using pygim::traits::reowned;
using pygim::traits::requalified;
using pygim::traits::unmangled;

consteval bool same(const std::string& made, std::string_view expected) { return made == expected; }

// ── the owner: a class name without its leading underscores ─────────────────────────────────
static_assert(owner_of("Trait") == "Trait");
static_assert(owner_of("_Trait") == "Trait");
static_assert(owner_of("__T_") == "T_");
static_assert(owner_of("__").empty());
static_assert(owner_of("").empty());

// ── which names Python mangles ─────────────────────────────────────────────────────────────────
static_assert(is_private_name("__secret"));
static_assert(is_private_name("__x_"));        // one trailing underscore is not two
static_assert(!is_private_name("__init__"));   // a dunder
static_assert(!is_private_name("__"));         // starts and ends with the same two underscores
static_assert(!is_private_name("___"));
static_assert(!is_private_name("_secret"));
static_assert(!is_private_name("secret"));
static_assert(!is_private_name("__pkg.mod"));  // a dotted name: an import of a package

// ── mangling ─────────────────────────────────────────────────────────────────────────────────
static_assert(same(mangled("Trait", "__secret"), "_Trait__secret"));
static_assert(same(mangled("_Trait", "__secret"), "_Trait__secret"));
static_assert(same(mangled("__T_", "__secret"), "_T___secret"));
static_assert(same(mangled("__", "__secret"), "__secret"));       // an all-underscore class mangles nothing
static_assert(same(mangled("Trait", "__init__"), "__init__"));
static_assert(same(mangled("Trait", "value"), "value"));

// ── unmangling ───────────────────────────────────────────────────────────────────────────────
static_assert(unmangled("Trait", "_Trait__secret") == "__secret");
static_assert(unmangled("_Trait", "_Trait__secret") == "__secret");
static_assert(unmangled("Trait", "_Other__secret").empty());      // another class's private name
static_assert(unmangled("A", "_Ab__x").empty());                  // a longer owner is another owner
static_assert(unmangled("Trait", "_Trait__init__").empty());      // not something mangling produces
static_assert(unmangled("Trait", "value").empty());
static_assert(unmangled("Trait", "_Trait").empty());
static_assert(unmangled("_", "__secret") == "__secret");          // unmangled where nothing is mangled
static_assert(unmangled("_", "value").empty());

// ── the law: unmangling undoes mangling, for every owner and private name ──────────────────────
consteval bool unmangling_undoes_mangling() {
    constexpr std::array<std::string_view, 5> classes{"Trait", "_Trait", "T", "__T_", "Ab"};
    constexpr std::array<std::string_view, 3> names{"__a", "__secret", "__x_"};
    for (auto c : classes)
        for (auto n : names)
            if (unmangled(c, mangled(c, n)) != n) return false;
    return true;
}
static_assert(unmangling_undoes_mangling());

// ── moving a name to another owner ──────────────────────────────────────────────────────────────
static_assert(same(reowned("Trait", "Target", "_Trait__secret"), "_Target__secret"));
static_assert(same(reowned("_Trait", "__Target", "_Trait__secret"), "_Target__secret"));
static_assert(same(reowned("Trait", "Target", "value"), "value"));
static_assert(same(reowned("Trait", "Target", "__init__"), "__init__"));
static_assert(same(reowned("Trait", "Target", "_Other__secret"), "_Other__secret"));
static_assert(same(reowned("_", "Target", "__secret"), "_Target__secret"));  // from a class that mangles nothing
static_assert(same(reowned("Trait", "_", "_Trait__secret"), "__secret"));    // to one

// ── the law: moving A -> B -> C is moving A -> C, for every name that is not B's own ───────────
consteval bool moves_compose() {
    constexpr std::array<std::string_view, 4> names{"_A__secret", "value", "__init__", "_Other__x"};
    for (auto n : names)
        if (reowned("B", "C", reowned("A", "B", n)) != reowned("A", "C", n)) return false;
    return true;
}
static_assert(moves_compose());
// The exception: code in A that already names B's private attribute. The chain moves it on to C,
// because after the first step it reads as B's own; the direct move leaves it as B's.
static_assert(same(reowned("B", "C", reowned("A", "B", "_B__x")), "_C__x"));
static_assert(same(reowned("A", "C", "_B__x"), "_B__x"));

// ── the qualified name ─────────────────────────────────────────────────────────────────────────
static_assert(same(requalified("Trait", "Target", "Trait.f"), "Target.f"));
static_assert(same(requalified("Trait", "Target", "Trait.f.<locals>.<lambda>"), "Target.f.<locals>.<lambda>"));
static_assert(same(requalified("mod.<locals>.Trait", "Target", "mod.<locals>.Trait.f"), "Target.f"));
static_assert(same(requalified("Trait", "Target", "TraitX.f"), "TraitX.f"));  // another owner
static_assert(same(requalified("Trait", "Target", "Trait"), "Trait"));        // the owner itself
static_assert(same(requalified("Trait", "Target", "f"), "f"));

}  // namespace
