// The reflection-era (C++26, P2996) shape of pygim's binding + materialisation
// layers. NOT part of the build: pygim's CI compilers predate reflection, which
// today ships experimentally in GCC 16.1+ (<meta>) and in the Bloomberg clang
// fork (godbolt id: clang_bb_p2996). Both compile this file:
//
//   g++-16  -std=c++26 -freflection                        reflection_bindings.cpp
//   clang++ -std=c++26 -freflection-latest -stdlib=libc++  reflection_bindings.cpp
//
// Every scenario below is a `static_assert`, so it is checked DURING COMPILATION —
// same discipline as layered_constexpr.cpp: the tests ARE the build. Once a CI
// compiler enables reflection (gate: __cpp_reflection), each section replaces
// hand-written code in pygim:
//
//  1. enum <-> string derived from the enum itself — retires the
//     parse_merge_strategy / merge_strategy_name switch pair (a two-sided drift
//     hazard today: adding a strategy means editing both in lockstep).
//  2. read_as<T> — fill a plain struct from a parsed document by enumerating its
//     members. This is the C++-consumer answer for pathlike: no variant document
//     tree, no per-type glue; serde-style.
//  3. member annotations (P3394) — merge strategies declared ON the fields:
//     [[=MergeStrategy::Sum]] int hp;  The merge trait reads them via reflection,
//     which is the purest form of "merge map as a trait over any struct".
//  4. bind_mapping<M> — enumerate a map's public interface and emit binding defs
//     from it (pybind11 mocked by a recorder; the def-emission loop is the part
//     reflection owns).

// GCC ships <meta>; the clang fork still spells it <experimental/meta>.
#if __has_include(<meta>)
#include <meta>
#else
#include <experimental/meta>
#endif

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace meta = std::meta;

// ── 1. enum <-> string, derived from the REAL toolkit enum ──────────────────
// The experiment compiles against the actual mapping-toolkit headers, so
// these proofs track the real MergeStrategy: adding an enumerator there
// updates both spelling directions here with no edit. (This costs godbolt
// single-file compiles — verification is the local gcc16 env and, later, the
// CI proof leg, both of which see the repo.)

#include "../src/_pygim_fast/mapping/merge.h"

using pygim::mapping::MergeStrategy;

// Cross-proof: the real strategy machinery is visible and behaves.
static_assert(pygim::mapping::merge_combine(MergeStrategy::Multiply, 30, 0) == 0);
static_assert(pygim::mapping::merge_combine(MergeStrategy::Sum, 30, 12) == 42);

constexpr char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

constexpr bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

consteval std::string_view strategy_name(MergeStrategy s) {
    template for (constexpr auto e :
                  std::define_static_array(meta::enumerators_of(^^pygim::mapping::MergeStrategy))) {
        if (s == [:e:]) return meta::identifier_of(e);
    }
    return "<unnamed>";
}

// Case-insensitive so the Python spelling ("sum") and the C++ one ("Sum") both
// resolve; -1 for a miss (kept as int to stay a plain literal type).
consteval int parse_strategy(std::string_view name) {
    template for (constexpr auto e :
                  std::define_static_array(meta::enumerators_of(^^pygim::mapping::MergeStrategy))) {
        if (iequal(name, meta::identifier_of(e))) return int([:e:]);
    }
    return -1;
}

static_assert(strategy_name(MergeStrategy::Deep) == "Deep");
static_assert(strategy_name(MergeStrategy::Multiply) == "Multiply");
static_assert(parse_strategy("sum") == int(MergeStrategy::Sum));
static_assert(parse_strategy("MULTIPLY") == int(MergeStrategy::Multiply));
static_assert(parse_strategy("average") == -1);
// Both directions track the enum: adding an enumerator updates them for free.
static_assert(meta::enumerators_of(^^pygim::mapping::MergeStrategy).size() == 8);

// ── 2. read_as<T>: struct materialisation from a parsed document ────────────

// Stand-in for what pathlike's engines produce (string keys, typed scalars).
using Scalar = std::variant<int, double, std::string>;

struct Doc {
    std::vector<std::pair<std::string, Scalar>> values;

    constexpr const Scalar* find(std::string_view key) const {
        for (const auto& [k, v] : values) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

template <typename T>
constexpr T read_as(const Doc& doc) {
    T out{};
    constexpr auto ctx = meta::access_context::unchecked();
    template for (constexpr auto m :
                  std::define_static_array(meta::nonstatic_data_members_of(^^T, ctx))) {
        if (const Scalar* v = doc.find(meta::identifier_of(m))) {
            out.[:m:] = std::get<typename[:meta::type_of(m):]>(*v);
        }
    }
    return out;
}

struct Character {
    int hp = 0;
    int speed = 0;
    std::string name;
};

consteval bool read_as_fills_members_by_name() {
    const Doc doc{{{"hp", 10}, {"speed", 30}, {"name", std::string("Ilse")}}};
    const Character c = read_as<Character>(doc);
    return c.hp == 10 && c.speed == 30 && c.name == "Ilse";
}

consteval bool read_as_leaves_absent_members_defaulted() {
    const Doc doc{{{"hp", 7}}};
    const Character c = read_as<Character>(doc);
    return c.hp == 7 && c.speed == 0 && c.name.empty();
}

static_assert(read_as_fills_members_by_name());
static_assert(read_as_leaves_absent_members_defaulted());

// ── 3. merge strategies as member annotations (P3394) ──────────────────────

struct Sheet {
    [[=MergeStrategy::Sum]] int hp = 0;
    [[=MergeStrategy::Multiply]] int speed = 0;
    std::string name;   // unannotated -> trait default (Replace)
};

// The strategy a member declares, or the fallback — this is the lookup a
// reflection-era merge trait performs instead of consulting per-key tables.
template <typename T>
consteval MergeStrategy declared_strategy(std::string_view member, MergeStrategy fallback) {
    constexpr auto ctx = meta::access_context::unchecked();
    template for (constexpr auto m :
                  std::define_static_array(meta::nonstatic_data_members_of(^^T, ctx))) {
        if (meta::identifier_of(m) == member) {
            template for (constexpr auto a : std::define_static_array(meta::annotations_of(m))) {
                if constexpr (meta::type_of(a) == ^^pygim::mapping::MergeStrategy) {
                    return meta::extract<MergeStrategy>(a);
                }
            }
        }
    }
    return fallback;
}

static_assert(declared_strategy<Sheet>("hp", MergeStrategy::Replace) == MergeStrategy::Sum);
static_assert(declared_strategy<Sheet>("speed", MergeStrategy::Replace) == MergeStrategy::Multiply);
static_assert(declared_strategy<Sheet>("name", MergeStrategy::Replace) == MergeStrategy::Replace);

// A merge step driven entirely by the annotations — the trait needs no runtime
// strategy storage at all for annotated structs.
consteval Sheet merge(const Sheet& lhs, const Sheet& rhs) {
    Sheet out = lhs;
    out.hp = declared_strategy<Sheet>("hp", MergeStrategy::Replace) == MergeStrategy::Sum
                 ? lhs.hp + rhs.hp
                 : rhs.hp;
    out.speed =
        declared_strategy<Sheet>("speed", MergeStrategy::Replace) == MergeStrategy::Multiply
            ? lhs.speed * rhs.speed
            : rhs.speed;
    out.name = rhs.name.empty() ? lhs.name : rhs.name;
    return out;
}

consteval bool annotations_drive_the_merge() {
    // Non-const locals: libstdc++'s constexpr std::string rejects aggregate-
    // initialising a const object (SSO buffer writes into a const subobject).
    Sheet base{.hp = 10, .speed = 30, .name = "Ilse"};
    Sheet paralyzed{.hp = 0, .speed = 0, .name = ""};
    Sheet merged = merge(base, paralyzed);
    return merged.hp == 10 && merged.speed == 0 && merged.name == "Ilse";
}

static_assert(annotations_drive_the_merge());

// ── 4. bind_mapping<M>: binding defs emitted from the class itself ──────────

// pybind11 stand-in: records what it was asked to bind. In the real binder the
// recorded (name, &[:fn:]) pairs become py::class_<M>(...).def(name, ptr).
struct class_recorder {
    std::vector<std::string> defs;

    template <typename F>
    constexpr class_recorder& def(std::string_view name, F /*member_ptr*/) {
        defs.emplace_back(name);
        return *this;
    }
};

// A mapping with the pygim surface shape (no overload sets: one name, one def).
class flat_map_mock {
public:
    constexpr bool contains(std::string_view) const { return false; }
    constexpr std::size_t size() const { return 0; }
    constexpr void merge_in(std::string_view, int) {}

private:
    std::vector<std::pair<std::string, int>> m_items;
    constexpr void rebalance() {}   // private: must NOT be bound
};

// The emission loop reflection owns: every public named member function of M
// becomes a def. Traits change what M has; the binder never changes.
template <typename M>
constexpr class_recorder bind_mapping() {
    class_recorder cls;
    constexpr auto ctx = meta::access_context::unchecked();
    template for (constexpr auto fn :
                  std::define_static_array(meta::members_of(^^M, ctx))) {
        if constexpr (meta::is_function(fn) && meta::is_public(fn) &&
                      meta::has_identifier(fn) && !meta::is_special_member_function(fn) &&
                      !meta::is_static_member(fn)) {
            cls.def(meta::identifier_of(fn), &[:fn:]);
        }
    }
    return cls;
}

consteval bool binder_emits_exactly_the_public_surface() {
    const class_recorder cls = bind_mapping<flat_map_mock>();
    return cls.defs == std::vector<std::string>{"contains", "size", "merge_in"};
}

static_assert(binder_emits_exactly_the_public_surface());

int main() { return 0; }   // every proof already ran at compile time
