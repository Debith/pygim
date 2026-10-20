// Compile-time proofs for utils/python_names.h — Python's naming conventions: the dunder rule of
// Python's enum machinery (enum._is_dunder), the private-name rule of its compiler (_Py_Mangle), and
// the move of a name and a qualified name from one owner class to another.
//
// Each function is proven by a case table (case_table.h, global #118): one row per call, the
// arguments first, the expected result after them, a case id last; within a table a baseline row
// comes first and each group below it changes one argument. The expected results are Python's own
// (_Py_Mangle, enum._is_dunder), written out by hand. The laws after the tables hold for every row
// of their own small products. The Python side of the dunder rule is proven against
// enum._is_dunder itself in tests/unittests/test_utils.py.
//
// This TU is part of the pygim.utils extension's sources (ext.utils.toml): every build compiles it,
// so a violated row cannot produce a binary, and the diagnostic names the row's index. It
// contributes no runtime code.

#include "../../src/_pygim_fast/utils/python_names.h"
#include "case_table.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace {

using pygim::proofs::every_row_holds;
using pygim::proofs::first_failing_row;
using pygim::python_names::is_dunder;
using pygim::python_names::is_private_name;
using pygim::python_names::mangled;
using pygim::python_names::NamePolicy;
using pygim::python_names::Reowning;
using pygim::python_names::owner_of;
using pygim::python_names::reowned;
using pygim::python_names::requalified;
using pygim::python_names::respelt;
using pygim::python_names::unmangled;

// ── is_dunder(name) -> bool, by enum._is_dunder ─────────────────────────────────────────────────
struct name_kind_row {
    std::string_view name;
    bool expected;
    std::string_view id;
};
constexpr name_kind_row is_dunder_rows[] = {
    //  name           expected   id
    {"__ab__",       true,  "baseline"},
    {"_ab__",        false, "leading_run_one"},          // vary the leading run of underscores
    {"___ab__",      false, "leading_run_three"},
    {"__ab_",        false, "trailing_run_one"},         // vary the trailing run
    {"__ab___",      false, "trailing_run_three"},
    {"____",         false, "body_empty"},               // vary the body between the runs
    {"__a__",        true,  "body_one_character"},
    {"__a_b__",      true,  "body_with_underscore"},
    {"__a__b__",     true,  "body_with_double_underscore"},
    {"__lt__",       true,  "protocol_method"},          // names Python itself uses
    {"__module__",   true,  "class_machinery"},
    {"",             false, "empty"},                    // boundaries
    {"_",            false, "one_underscore"},
    {"__",           false, "two_underscores"},
    {"___",          false, "three_underscores"},
    {"_____",        false, "five_underscores"},
    {"__x",          false, "private_name"},             // other kinds of name
    {"x__",          false, "trailing_only"},
    {"_x_",          false, "single_underscores"},
    {"_Trait__x",    false, "mangled_name"},
    {"x",            false, "plain"},
};
static_assert(first_failing_row(is_dunder_rows, [](const auto& r) { return is_dunder(r.name) == r.expected; })
              == every_row_holds);

// ── is_private_name(name) -> bool, by _Py_Mangle ────────────────────────────────────────────────
constexpr name_kind_row is_private_name_rows[] = {
    //  name           expected   id
    {"__secret",     true,  "baseline"},
    {"_secret",      false, "leading_run_one"},          // vary the leading run
    {"secret",       false, "leading_run_none"},
    {"__x_",         true,  "trailing_run_one"},         // vary the trailing run
    {"__init__",     false, "trailing_run_two"},         // a dunder is not mangled
    {"__",           false, "only_the_leading_run"},     // boundaries
    {"___",          false, "three_underscores"},
    {"",             false, "empty"},
    {"__pkg.mod",    false, "dotted"},                   // a dotted name: an import of a package
};
static_assert(first_failing_row(is_private_name_rows,
                                [](const auto& r) { return is_private_name(r.name) == r.expected; })
              == every_row_holds);

// ── owner_of(class_name) -> the part mangling uses ──────────────────────────────────────────────
struct owner_row {
    std::string_view class_name;
    std::string_view expected;
    std::string_view id;
};
constexpr owner_row owner_of_rows[] = {
    //  class_name   expected   id
    {"Trait",      "Trait",   "baseline"},
    {"_Trait",     "Trait",   "leading_underscore"},     // vary the leading underscores
    {"__T_",       "T_",      "leading_two_trailing_one"},
    {"__",         "",        "only_underscores"},       // boundaries: such a class mangles nothing
    {"",           "",        "empty"},
};
static_assert(first_failing_row(owner_of_rows, [](const auto& r) { return owner_of(r.class_name) == r.expected; })
              == every_row_holds);

// ── mangled(class_name, name) -> the spelling inside the class ──────────────────────────────────
struct in_class_row {
    std::string_view class_name;
    std::string_view name;
    std::string_view expected;
    std::string_view id;
};
constexpr in_class_row mangled_rows[] = {
    //  class_name   name           expected            id
    {"Trait",      "__secret",    "_Trait__secret",   "baseline"},
    {"_Trait",     "__secret",    "_Trait__secret",   "class_leading_underscore"},   // vary class_name
    {"__T_",       "__secret",    "_T___secret",      "class_trailing_underscore"},
    {"__",         "__secret",    "__secret",         "class_only_underscores"},
    {"",           "__secret",    "__secret",         "class_empty"},
    {"Trait",      "__x_",        "_Trait__x_",       "name_trailing_one"},          // vary name
    {"Trait",      "__init__",    "__init__",         "name_dunder"},
    {"Trait",      "_secret",     "_secret",          "name_one_underscore"},
    {"Trait",      "value",       "value",            "name_public"},
    {"Trait",      "__pkg.mod",   "__pkg.mod",        "name_dotted"},
};
static_assert(first_failing_row(mangled_rows,
                                [](const auto& r) { return mangled(r.class_name, r.name) == r.expected; })
              == every_row_holds);

// ── unmangled(class_name, name) -> the private name it spells, or "" when not the class's own ───
constexpr in_class_row unmangled_rows[] = {
    //  class_name   name               expected     id
    {"Trait",      "_Trait__secret",  "__secret",  "baseline"},
    {"_Trait",     "_Trait__secret",  "__secret",  "class_leading_underscore"},   // vary class_name
    {"Other",      "_Trait__secret",  "",          "class_another"},
    {"Trai",       "_Trait__secret",  "",          "class_a_prefix_of_the_owner"},
    {"_",          "_Trait__secret",  "",          "class_only_underscores"},
    {"Trait",      "_Other__secret",  "",          "name_another_owner"},         // vary name
    {"Trait",      "_Trait__init__",  "",          "name_not_from_mangling"},
    {"Trait",      "_Trait",          "",          "name_owner_alone"},
    {"Trait",      "value",           "",          "name_public"},
    {"Trait",      "__secret",        "",          "name_unmangled_private"},
    {"_",          "__secret",        "__secret",  "only_underscores_private"},   // a class that mangles nothing
    {"_",          "value",           "",          "only_underscores_public"},
};
static_assert(first_failing_row(unmangled_rows,
                                [](const auto& r) { return unmangled(r.class_name, r.name) == r.expected; })
              == every_row_holds);

// ── respelt(from, to, name) -> the new spelling, or nothing when the name does not move ─────────
struct respelt_row {
    std::string_view from;
    std::string_view to;
    std::string_view name;
    std::optional<std::string_view> expected;
    std::string_view id;
};
constexpr respelt_row respelt_rows[] = {
    //  from       to          name               expected            id
    {"Trait",    "Target",   "_Trait__secret",  "_Target__secret",  "baseline"},
    {"Trait",    "__Target", "_Trait__secret",  "_Target__secret",  "to_leading_underscores"},  // vary to
    {"Trait",    "_",        "_Trait__secret",  "__secret",         "to_only_underscores"},
    {"_Trait",   "Target",   "_Trait__secret",  "_Target__secret",  "from_leading_underscore"}, // vary from
    {"Other",    "Target",   "_Trait__secret",  std::nullopt,       "from_another"},
    {"_",        "Target",   "__secret",        "_Target__secret",  "from_only_underscores"},
    {"Trait",    "Target",   "value",           std::nullopt,       "name_public"},             // vary name
    {"Trait",    "Target",   "__init__",        std::nullopt,       "name_dunder"},
    {"Trait",    "Target",   "_Other__x",       std::nullopt,       "name_another_owner"},
};
static_assert(first_failing_row(respelt_rows, [](const auto& r) {
                  const auto moved = respelt(r.from, r.to, r.name);
                  return moved.has_value() == r.expected.has_value() && (!moved || *moved == *r.expected);
              }) == every_row_holds);

// ── reowned(from, to, name) -> respelt's spelling, or the name itself ───────────────────────────
struct move_row {
    std::string_view from;
    std::string_view to;
    std::string_view name;
    std::string_view expected;
    std::string_view id;
};
constexpr move_row reowned_rows[] = {
    //  from       to          name               expected           id
    {"Trait",    "Target",   "_Trait__secret",  "_Target__secret", "baseline"},
    {"Trait",    "__Target", "_Trait__secret",  "_Target__secret", "to_leading_underscores"},  // vary to
    {"Trait",    "_",        "_Trait__secret",  "__secret",        "to_only_underscores"},
    {"_Trait",   "Target",   "_Trait__secret",  "_Target__secret", "from_leading_underscore"}, // vary from
    {"_",        "Target",   "__secret",        "_Target__secret", "from_only_underscores"},
    {"Trait",    "Target",   "value",           "value",           "name_public"},             // vary name
    {"Trait",    "Target",   "__init__",        "__init__",        "name_dunder"},
    {"Trait",    "Target",   "_Other__secret",  "_Other__secret",  "name_another_owner"},
};
static_assert(first_failing_row(reowned_rows,
                                [](const auto& r) { return reowned(r.from, r.to, r.name) == r.expected; })
              == every_row_holds);

// ── requalified(from, to, qualname) -> the qualified name under its new owner ───────────────────
constexpr move_row requalified_rows[] = {
    //  from                   to          qualname                        expected                         id
    {"Trait",              "Target",   "Trait.f",                      "Target.f",                      "baseline"},
    {"Trait",              "Target",   "Trait.f.<locals>.<lambda>",    "Target.f.<locals>.<lambda>",    "qualname_nested"},   // vary qualname
    {"Trait",              "Target",   "TraitX.f",                     "TraitX.f",                      "qualname_another_owner"},
    {"Trait",              "Target",   "Trait",                        "Trait",                         "qualname_the_owner"},
    {"Trait",              "Target",   "f",                            "f",                             "qualname_no_owner"},
    {"mod.<locals>.Trait", "Target",   "mod.<locals>.Trait.f",         "Target.f",                      "from_local_class"},  // vary from
};
static_assert(first_failing_row(requalified_rows,
                                [](const auto& r) { return requalified(r.from, r.to, r.name) == r.expected; })
              == every_row_holds);

// ── the laws, over every pairing of their rows ──────────────────────────────────────────────────

// A dunder is never mangled, and a mangled name is never a dunder.
static_assert(!is_private_name("__lt__") && is_dunder("__lt__"));
static_assert(!is_dunder("_Trait__x"));

// Unmangling undoes mangling, for every owner and private name.
consteval bool unmangling_undoes_mangling() {
    constexpr std::array<std::string_view, 5> classes{"Trait", "_Trait", "T", "__T_", "Ab"};
    constexpr std::array<std::string_view, 3> names{"__a", "__secret", "__x_"};
    for (auto c : classes)
        for (auto n : names)
            if (unmangled(c, mangled(c, n)) != n) return false;
    return true;
}
static_assert(unmangling_undoes_mangling());

// reowned is respelt's new spelling, or the name itself.
consteval bool reowned_is_respelt_or_the_name() {
    constexpr std::array<std::string_view, 6> names{"_A__secret", "value", "__init__", "_Other__x", "_A", "__x"};
    for (auto n : names) {
        const auto moved = respelt("A", "B", n);
        if (reowned("A", "B", n) != (moved ? *moved : std::string{n})) return false;
    }
    return true;
}
static_assert(reowned_is_respelt_or_the_name());

// Moving A -> B -> C is moving A -> C, for every name that is not B's own.
consteval bool moves_compose() {
    constexpr std::array<std::string_view, 4> names{"_A__secret", "value", "__init__", "_Other__x"};
    for (auto n : names)
        if (reowned("B", "C", reowned("A", "B", n)) != reowned("A", "C", n)) return false;
    return true;
}
static_assert(moves_compose());
// The exception: code in A that already names B's private attribute. The chain moves it on to C,
// because after the first step it reads as B's own; the direct move leaves it as B's.
static_assert(reowned("B", "C", reowned("A", "B", "_B__x")) == "_C__x");
static_assert(reowned("A", "C", "_B__x") == "_B__x");

// NamePolicy gathers the rules for one class; it is a thin value over the free functions above, so it
// must agree with them. (The traits adapters use it, so the whole traits suite exercises it too.)
consteval bool name_policy_agrees_with_the_free_functions() {
    const NamePolicy daa{"daa"};
    if (daa.mangled("__attr") != "_daa__attr" || daa.mangled("plain") != "plain") return false;
    if (daa.unmangled("_daa__attr") != "__attr") return false;
    const NamePolicy a{"A"};
    const NamePolicy b{"B"};
    if (const auto moved = a.respelt(b, "_A__x"); !moved || *moved != "_B__x") return false;
    if (a.respelt(b, "value")) return false;                 // a public name does not move: nothing
    if (a.reowned(b, "_A__x") != "_B__x" || a.reowned(b, "value") != "value") return false;
    const NamePolicy from{"A", "A.f"};
    const NamePolicy to{"B", "B.f"};
    if (from.requalified(to, "A.f.<locals>.g") != "B.f.<locals>.g") return false;
    if (NamePolicy{"Inner", "Outer.Inner", "pkg.mod"}.qualified_name() != "pkg.mod.Outer.Inner") return false;
    if (NamePolicy{"Inner", "Outer.Inner"}.qualified_name() != "Outer.Inner") return false;  // no module
    return true;
}
static_assert(name_policy_agrees_with_the_free_functions());

// Reowning applies NamePolicy's moves in order: it must agree with the chained free functions,
// the exception included, and then() must leave the reowning it extends as it was.
consteval bool reowning_applies_the_moves_in_order() {
    const Reowning none;
    if (none.respelt("_A__x") || none.requalified("A.f") != "A.f") return false;   // no moves: nothing moves
    const Reowning ab = none.then(NamePolicy{"A"}, NamePolicy{"B"});
    if (ab.respelt("_A__x") != "_B__x" || ab.respelt("value") || ab.respelt("_Other__x")) return false;
    if (ab.requalified("A.f.<locals>.g") != "B.f.<locals>.g" || ab.requalified("AX.f") != "AX.f") return false;
    const Reowning abc = ab.then(NamePolicy{"B"}, NamePolicy{"C"});
    if (abc.respelt("_A__x") != reowned("B", "C", reowned("A", "B", "_A__x"))) return false;   // moved twice
    if (abc.respelt("_B__x") != "_C__x") return false;      // the exception: B's own after A -> B, moved on
    if (abc.respelt("_C__x")) return false;                 // C's own was never A's or B's
    if (abc.requalified("A.f") != "C.f") return false;
    if (ab.respelt("_A__x") != "_B__x" || none.respelt("_A__x")) return false;   // then() changed neither
    const Reowning qualified = none.then(NamePolicy{"T", "Outer.T"}, NamePolicy{"U", "Other.U"});
    if (qualified.respelt("_T__x") != "_U__x" || qualified.requalified("Outer.T.f") != "Other.U.f") return false;
    return true;
}
static_assert(reowning_applies_the_moves_in_order());

}  // namespace
