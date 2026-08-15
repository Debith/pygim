// Compile-time proofs for mapping/storage.h + mapping/gimmap.h.
//
// This TU is part of the pygim.utils extension's SOURCES (see ext.utils.toml):
// it is compiled by every build, so a violated invariant cannot produce a
// binary. It contributes no runtime code. Short spot-asserts stay next to the
// definitions; everything exhaustive lives here.
//
// The suites run on flat_storage — the constexpr engine — so every law is
// evaluated during compilation. Trait proofs additionally need deducing this
// (C++23, GCC 14+); on an older frontend they drop out while the storage-law
// and frozen-surface proofs still run (graduated coverage, see gimmap.h).

#include "../../src/_pygim_fast/mapping/hooks.h"    // includes gimmap.h -> storage.h
#include "../../src/_pygim_fast/mapping/layers.h"   // includes merge.h -> gimmap.h

namespace {

using pygim::mapping::gimmap;
using pygim::mapping::flat_storage;
using pygim::mapping::storage;

using FS = flat_storage<int, int>;

// ── storage laws (flat engine) ──────────────────────────────────────────────

static_assert(storage<FS>);
static_assert(FS::ordered);

consteval bool find_hits_and_misses() {
    FS s{{2, 20}, {1, 10}};
    return s.find(1) != nullptr && *s.find(1) == 10 &&
           s.find(2) != nullptr && *s.find(2) == 20 &&
           s.find(3) == nullptr;
}
static_assert(find_hits_and_misses());

consteval bool insert_is_insert_or_assign() {
    FS s;
    s.insert(1, 10);
    s.insert(1, 11);   // same key: assigns, does not duplicate
    return s.size() == 1 && *s.find(1) == 11;
}
static_assert(insert_is_insert_or_assign());

consteval bool erase_removes_and_reports() {
    FS s{{1, 10}, {2, 20}};
    const bool hit = s.erase(1);
    const bool miss = s.erase(99);
    return hit && !miss && s.size() == 1 && s.find(1) == nullptr && s.find(2) != nullptr;
}
static_assert(erase_removes_and_reports());

consteval bool items_stay_key_sorted() {
    FS s;
    for (int key : {5, 1, 4, 2, 3}) s.insert(key, key * 10);
    int previous = 0;
    for (const auto& [key, value] : s.items()) {
        if (key <= previous || value != key * 10) return false;
        previous = key;
    }
    return s.size() == 5;
}
static_assert(items_stay_key_sorted());

consteval bool clear_empties() {
    FS s{{1, 10}};
    s.clear();
    return s.empty() && s.size() == 0 && s.find(1) == nullptr;
}
static_assert(clear_empties());

// ── frozen base surface ─────────────────────────────────────────────────────

using Frozen = gimmap<FS>;

consteval bool frozen_reads_work() {
    const Frozen m{{1, 10}, {2, 20}};
    return m.contains(1) && !m.contains(3) && m.at(2) == 20 &&
           m.value_or(1, -1) == 10 && m.value_or(3, -1) == -1 &&
           m.size() == 2 && !m.empty();
}
static_assert(frozen_reads_work());

consteval bool at_returns_present_values() {
    const Frozen m{{1, 10}};
    // at()'s throwing path cannot be proven here — reaching a throw in a
    // consteval context is a compile error — so it is pinned by the runtime
    // KeyError tests when the Python surface lands (step 3).
    return m.contains(1) && m.at(1) == 10;
}
static_assert(at_returns_present_values());

consteval bool equality_is_content_equality() {
    const Frozen a{{1, 10}, {2, 20}};
    const Frozen b{{2, 20}, {1, 10}};   // same content, different insert order
    const Frozen c{{1, 10}};
    return a == b && !(a == c);
}
static_assert(equality_is_content_equality());

// ── negative composition proofs: frozen provably lacks mutation ─────────────

template <typename M>
concept exposes_set = requires(M m) { m.set(1, 2); };
template <typename M>
concept exposes_erase = requires(M m) { m.erase(1); };
template <typename M>
concept exposes_clear = requires(M m) { m.clear(); };

static_assert(!exposes_set<Frozen>);
static_assert(!exposes_erase<Frozen>);
static_assert(!exposes_clear<Frozen>);

// ── mutable trait (needs deducing this) ─────────────────────────────────────

#if PYGIM_HAS_DEDUCING_THIS

using pygim::mapping::has_mutable;
using pygim::mapping::mutable_trait;

using Mut = gimmap<FS, mutable_trait>;

static_assert(has_mutable<Mut> && !has_mutable<Frozen>);
static_assert(exposes_set<Mut> && exposes_erase<Mut> && exposes_clear<Mut>);

consteval bool mutation_roundtrip() {
    Mut m;
    m.set(1, 10);
    m.set(2, 20);
    m.set(1, 11);                        // assign through the trait
    const bool shaped = m.size() == 2 && m.at(1) == 11;
    const bool erased = m.erase(2) && !m.erase(2);
    m.clear();
    return shaped && erased && m.empty();
}
static_assert(mutation_roundtrip());

consteval bool freeze_drops_mutation_keeps_content() {
    Mut m;
    m.set(1, 10);
    m.set(2, 20);
    auto frozen = std::move(m).freeze();
    static_assert(!exposes_set<decltype(frozen)>);   // the TYPE lost set()
    return frozen.size() == 2 && frozen.at(1) == 10 && frozen.at(2) == 20;
}
static_assert(freeze_drops_mutation_keeps_content());

consteval bool thaw_restores_mutation_on_a_copy() {
    const Frozen f{{1, 10}};
    auto m = f.thaw<mutable_trait>();
    m.set(2, 20);
    return m.size() == 2 &&        // the thawed copy mutated...
           f.size() == 1;          // ...the frozen original did not
}
static_assert(thaw_restores_mutation_on_a_copy());

consteval bool lvalue_freeze_copies() {
    Mut m;
    m.set(1, 10);
    auto frozen = m.freeze();      // const& overload: copy, source stays usable
    m.set(2, 20);
    return frozen.size() == 1 && m.size() == 2;
}
static_assert(lvalue_freeze_copies());

// ── merge trait: application laws (merge_combine, plain value domain) ───────

using pygim::mapping::merge_combine;
using pygim::mapping::MergeStrategy;

static_assert(merge_combine(MergeStrategy::Sum, 30, 12) == 42);
static_assert(merge_combine(MergeStrategy::Max, 30, 12) == 30);
static_assert(merge_combine(MergeStrategy::Min, 30, 12) == 12);
static_assert(merge_combine(MergeStrategy::Replace, 30, 12) == 12);
static_assert(merge_combine(MergeStrategy::Multiply, 30, 0) == 0);      // Speed x0
static_assert(merge_combine(MergeStrategy::Extend, 30, 12) == 42);      // degrades to Sum
static_assert(merge_combine(MergeStrategy::Union, 30, 12) == 12);       // scalar: replace
static_assert(merge_combine(MergeStrategy::Deep, 30, 12) == 12);        // scalar: replace

// ── merge trait: resolution laws (per-key beats default) ────────────────────

using pygim::mapping::has_merge;
using pygim::mapping::merge_trait;

using MutMerge = gimmap<FS, pygim::mapping::mutable_trait, merge_trait<int>>;
using FrozenMerge = gimmap<FS, merge_trait<int>>;

static_assert(has_merge<MutMerge> && has_merge<FrozenMerge> && !has_merge<Mut>);

consteval bool per_key_strategy_beats_default() {
    MutMerge m;
    m.set_default_strategy(MergeStrategy::Sum);
    m.set_merge_strategy(1, MergeStrategy::Max);
    return m.strategy_for(1) == MergeStrategy::Max &&
           m.strategy_for(2) == MergeStrategy::Sum;      // unlisted key: default
}
static_assert(per_key_strategy_beats_default());

consteval bool default_strategy_is_replace_until_set() {
    const MutMerge m;
    return m.default_strategy() == MergeStrategy::Replace;
}
static_assert(default_strategy_is_replace_until_set());

// ── merge as an operation: | folds and returns the FROZEN base type ─────────

consteval bool merged_folds_and_freezes() {
    MutMerge a;
    a.set_default_strategy(MergeStrategy::Sum);
    a.set(1, 10);
    a.set(2, 20);
    MutMerge b;
    b.set(1, 32);       // shared key: folds by A's strategy (target decides)
    b.set(3, 30);       // new key: inserted
    auto folded = a | b;
    static_assert(std::is_same_v<decltype(folded), gimmap<FS>>);   // FROZEN
    static_assert(!exposes_set<decltype(folded)>);
    return folded.at(1) == 42 && folded.at(2) == 20 && folded.at(3) == 30 &&
           a.size() == 2 && b.size() == 2;               // both sides untouched
}
static_assert(merged_folds_and_freezes());

// ── in-place accumulator surface (needs mutable; unbounded accumulation) ────

template <typename M>
concept exposes_merge_in = requires(M m) { m.merge_in(1, 2); };

static_assert(exposes_merge_in<MutMerge>);
static_assert(!exposes_merge_in<FrozenMerge>);   // frozen+merge folds functionally only
static_assert(!exposes_merge_in<Mut>);           // mutation alone doesn't merge

consteval bool merge_in_accumulates_to_one_value() {
    MutMerge m;
    m.set_default_strategy(MergeStrategy::Sum);
    for (int sample = 1; sample <= 100; ++sample) m.merge_in(7, sample);
    return m.size() == 1 && m.at(7) == 5050;     // folded, not 100 contributions
}
static_assert(merge_in_accumulates_to_one_value());

consteval bool merge_with_folds_whole_map() {
    MutMerge target;
    target.set_default_strategy(MergeStrategy::Max);
    target.set(1, 10);
    Mut other;                                    // any map with items() works
    other.set(1, 7);
    other.set(2, 20);
    target.merge_with(other);
    return target.at(1) == 10 && target.at(2) == 20;
}
static_assert(merge_with_folds_whole_map());

// ── layer trait: merge with memory (provenance + undo) ──────────────────────

using pygim::mapping::has_layers_trait;
using pygim::mapping::layer_trait;
using pygim::mapping::traits_dependencies_ok;

using Sheet = gimmap<FS, pygim::mapping::mutable_trait, merge_trait<int>,
                     layer_trait<int, int>>;

static_assert(has_layers_trait<Sheet> && !has_layers_trait<MutMerge>);

// The dependency gate: layers demand merge + mutable, in any order.
static_assert(!traits_dependencies_ok<layer_trait<int, int>>());
static_assert(!traits_dependencies_ok<pygim::mapping::mutable_trait, layer_trait<int, int>>());
static_assert(!traits_dependencies_ok<merge_trait<int>, layer_trait<int, int>>());
static_assert(traits_dependencies_ok<pygim::mapping::mutable_trait, merge_trait<int>,
                                     layer_trait<int, int>>());

template <typename M>
concept exposes_apply = requires(M m) { m.apply("who", 1, 2); };
static_assert(exposes_apply<Sheet>);
static_assert(!exposes_apply<MutMerge>);      // merging alone has no layers

consteval bool layered_removal_restores_the_old_value() {
    Sheet s;
    s.set(1, 30);                                          // base speed 30
    s.apply("haste", 1, MergeStrategy::Multiply, 2);       // x2 -> 60
    s.apply("paralyzed", 1, MergeStrategy::Multiply, 0);   // x0 -> 0
    const bool paralyzed = s.observe(1) == 0;
    s.remove("paralyzed");                                 // lift the condition
    return paralyzed && s.observe(1) == 60;                // no inverse math
}
static_assert(layered_removal_restores_the_old_value());

consteval bool first_contribution_establishes_without_base() {
    Sheet s;                                               // no base for key 7
    s.apply("blessing", 7, MergeStrategy::Sum, 5);
    s.apply("blessing", 7, MergeStrategy::Sum, 3);
    return s.observe(7) == 8 && s.holds(7) && !s.contains(7);   // layers-only key
}
static_assert(first_contribution_establishes_without_base());

consteval bool provenance_answers_who_and_where() {
    Sheet s;
    s.set(1, 10);
    s.apply("race", 1, MergeStrategy::Sum, 2);
    s.apply("race", 2, MergeStrategy::Sum, 5);
    s.apply("item", 1, MergeStrategy::Max, 4);
    const auto who = s.sources(1);                         // application order
    const auto where = s.footprint("race");                // reverse index
    return who.size() == 2 && who[0] == "race" && who[1] == "item" &&
           where.size() == 2 && where[0] == 1 && where[1] == 2;
}
static_assert(provenance_answers_who_and_where());

consteval bool snapshot_is_frozen_and_independent() {
    Sheet s;
    s.set(1, 10);
    s.apply("race", 1, MergeStrategy::Sum, 2);
    auto snap = s.snapshot();
    static_assert(std::is_same_v<decltype(snap), gimmap<FS>>);   // the frozen type
    static_assert(!exposes_set<decltype(snap)>);
    s.apply("curse", 1, MergeStrategy::Sum, -100);         // later edit
    return snap.at(1) == 12 && s.observe(1) == -88;        // snapshot untouched
}
static_assert(snapshot_is_frozen_and_independent());

// ── hooks + strict: the Registry-convergence pair ───────────────────────────

using pygim::mapping::has_hooks;
using pygim::mapping::has_strict;
using pygim::mapping::hooks_trait;
using pygim::mapping::strict_trait;

// Constexpr-friendly callback for the proofs (std::function is runtime-only).
struct CountingHook {
    int* count;
    constexpr void operator()(const int&, const int&) const { ++*count; }
};

using Hooked = gimmap<FS, pygim::mapping::mutable_trait,
                      hooks_trait<int, int, CountingHook>>;

static_assert(has_hooks<Hooked> && !has_hooks<Mut>);
static_assert(!traits_dependencies_ok<strict_trait>());   // strict writes: needs mutable
static_assert(traits_dependencies_ok<pygim::mapping::mutable_trait, strict_trait>());

// Surface honesty: a hookless map HAS no add_on_register — the Registry
// silent-discard misuse class is unrepresentable in the toolkit.
template <typename M>
concept exposes_add_on_register = requires(M m, CountingHook h) { m.add_on_register(h); };
static_assert(exposes_add_on_register<Hooked>);
static_assert(!exposes_add_on_register<Mut>);
static_assert(!exposes_add_on_register<Frozen>);

consteval bool hooks_fire_on_every_write() {
    int fired = 0;
    Hooked m;
    m.add_on_register(CountingHook{&fired});
    m.add_on_register(CountingHook{&fired});   // two hooks registered
    m.set(1, 10);
    m.set(2, 20);
    return fired == 4 && m.at(1) == 10;        // 2 hooks x 2 writes
}
static_assert(hooks_fire_on_every_write());

consteval bool strict_insert_and_override_route_through_hooks() {
    int fired = 0;
    gimmap<FS, pygim::mapping::mutable_trait, strict_trait,
           hooks_trait<int, int, CountingHook>> m;
    m.add_on_register(CountingHook{&fired});
    m.register_or_override(1, 10, false);      // new key: plain insert
    m.register_or_override(1, 11, true);       // existing key: override
    return m.at(1) == 11 && fired == 2;        // hooks fired for BOTH writes
}
static_assert(strict_insert_and_override_route_through_hooks());
// (The throwing paths — duplicate insert, override-of-missing — cannot be
// proven in consteval; they stay pinned by Registry's runtime tests.)

#endif  // PYGIM_HAS_DEDUCING_THIS

}  // namespace
