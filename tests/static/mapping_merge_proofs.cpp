// Compile-time proofs for MERGE BEHAVIOR across use-cases.
//
// This TU is part of the pygim.utils extension's SOURCES (see ext.utils.toml):
// every build proves these laws or fails. Where mapping_core_proofs.cpp proves
// the toolkit's structure (concepts, trait composition, surface honesty),
// this file proves merge SEMANTICS scenario by scenario: application laws,
// resolution precedence, fold algebra, ordering, aliasing, accumulation,
// layered capture/undo, and the hook interplay. Sections below the deducing-
// this gate need GCC 14+; the application-law section runs on every frontend.

#include "../../src/_pygim_fast/mapping/hooks.h"
#include "../../src/_pygim_fast/mapping/layers.h"

namespace {

using pygim::mapping::flat_storage;
using pygim::mapping::gimmap;
using pygim::mapping::merge_combine;
using pygim::mapping::MergeDefaultStrategy;
using pygim::mapping::MergeStrategy;

// ════════════════════════════════════════════════════════════════════════════
// 1. APPLICATION LAWS — merge_combine over plain values (no frontend gate)
// ════════════════════════════════════════════════════════════════════════════

// Sum / Extend: arithmetic addition; Extend degrades to Sum for scalars.
static_assert(merge_combine(MergeStrategy::Sum, 30, 12) == 42);
static_assert(merge_combine(MergeStrategy::Sum, -5, 5) == 0);
static_assert(merge_combine(MergeStrategy::Sum, 1.5, 2.25) == 3.75);
static_assert(merge_combine(MergeStrategy::Extend, 30, 12) == 42);

// Multiply: rider effects — Haste doubles, Paralyzed zeroes, signs behave.
static_assert(merge_combine(MergeStrategy::Multiply, 30, 2) == 60);
static_assert(merge_combine(MergeStrategy::Multiply, 30, 0) == 0);
static_assert(merge_combine(MergeStrategy::Multiply, -3, 4) == -12);
static_assert(merge_combine(MergeStrategy::Multiply, 0.5, 0.5) == 0.25);

// Max / Min: pick a side, never invent a value.
static_assert(merge_combine(MergeStrategy::Max, 30, 12) == 30);
static_assert(merge_combine(MergeStrategy::Max, 12, 30) == 30);
static_assert(merge_combine(MergeStrategy::Min, 30, 12) == 12);
static_assert(merge_combine(MergeStrategy::Min, -1, 1) == -1);

// Replace: rhs wins unconditionally.
static_assert(merge_combine(MergeStrategy::Replace, 30, 12) == 12);
static_assert(merge_combine(MergeStrategy::Replace, 0, 0) == 0);

// Union / Deep concern containers; for scalars they resolve to replace.
static_assert(merge_combine(MergeStrategy::Union, 30, 12) == 12);
static_assert(merge_combine(MergeStrategy::Deep, 30, 12) == 12);

// Idempotence where the law promises it: Max/Min/Replace of x with x is x.
static_assert(merge_combine(MergeStrategy::Max, 7, 7) == 7);
static_assert(merge_combine(MergeStrategy::Min, 7, 7) == 7);
static_assert(merge_combine(MergeStrategy::Replace, 7, 7) == 7);

// Type defaults: arithmetic accumulates, everything else replaces.
static_assert(MergeDefaultStrategy<int>::value == MergeStrategy::Sum);
static_assert(MergeDefaultStrategy<double>::value == MergeStrategy::Sum);
static_assert(MergeDefaultStrategy<const char*>::value == MergeStrategy::Replace);

#if PYGIM_HAS_DEDUCING_THIS

using pygim::mapping::hooks_trait;
using pygim::mapping::layer_trait;
using pygim::mapping::merge_trait;
using pygim::mapping::mutable_trait;

using FS = flat_storage<int, int>;
using M = gimmap<FS, mutable_trait, merge_trait<int>>;
using Frozen = gimmap<FS>;

// ════════════════════════════════════════════════════════════════════════════
// 2. RESOLUTION PRECEDENCE — per-key beats default, default is explicit
// ════════════════════════════════════════════════════════════════════════════

consteval bool per_key_wins_and_default_covers_the_rest() {
    M m;
    m.set_default_strategy(MergeStrategy::Sum);
    m.set_merge_strategy(1, MergeStrategy::Max);
    m.set_merge_strategy(2, MergeStrategy::Multiply);
    return m.strategy_for(1) == MergeStrategy::Max &&
           m.strategy_for(2) == MergeStrategy::Multiply &&
           m.strategy_for(99) == MergeStrategy::Sum;
}
static_assert(per_key_wins_and_default_covers_the_rest());

consteval bool per_key_table_survives_reconfiguration() {
    M m;
    m.set_merge_strategy(1, MergeStrategy::Max);
    m.set_default_strategy(MergeStrategy::Multiply);     // later default change
    return m.strategy_for(1) == MergeStrategy::Max;      // per-key unaffected
}
static_assert(per_key_table_survives_reconfiguration());

// ════════════════════════════════════════════════════════════════════════════
// 3. ACCUMULATION — merge_in as the in-place fold (counters, telemetry)
// ════════════════════════════════════════════════════════════════════════════

consteval bool first_write_inserts_regardless_of_strategy() {
    M m;
    m.set_default_strategy(MergeStrategy::Multiply);     // would zero on fold...
    m.merge_in(1, 30);                                   // ...but first write inserts
    return m.at(1) == 30;
}
static_assert(first_write_inserts_regardless_of_strategy());

consteval bool sum_accumulates_and_replace_keeps_last() {
    M m;
    m.set_default_strategy(MergeStrategy::Sum);
    for (int sample = 1; sample <= 100; ++sample) m.merge_in(1, sample);
    m.set_merge_strategy(2, MergeStrategy::Replace);
    m.merge_in(2, 10);
    m.merge_in(2, 20);
    m.merge_in(2, 30);
    return m.at(1) == 5050 && m.at(2) == 30 && m.size() == 2;
}
static_assert(sum_accumulates_and_replace_keeps_last());

consteval bool max_is_monotone_under_accumulation() {
    M m;
    m.set_default_strategy(MergeStrategy::Max);
    for (int v : {3, 9, 1, 9, 4}) m.merge_in(1, v);
    return m.at(1) == 9;
}
static_assert(max_is_monotone_under_accumulation());

consteval bool multiply_chain_zeroes_and_stays_zero() {
    M m;
    m.set_default_strategy(MergeStrategy::Multiply);
    m.merge_in(1, 30);       // establish
    m.merge_in(1, 2);        // haste: 60
    m.merge_in(1, 0);        // paralyzed: 0
    m.merge_in(1, 2);        // haste again: still 0 — eager fold has no undo
    return m.at(1) == 0;     // (the undo story is the LAYERED map's job)
}
static_assert(multiply_chain_zeroes_and_stays_zero());

// ════════════════════════════════════════════════════════════════════════════
// 4. WHOLE-MAP FOLD ALGEBRA — merged()/operator| and merge_with()
// ════════════════════════════════════════════════════════════════════════════

consteval bool target_strategies_decide_the_fold() {
    M a;
    a.set_default_strategy(MergeStrategy::Sum);
    a.set(1, 10);
    M b;
    b.set_default_strategy(MergeStrategy::Multiply);     // contributor's config...
    b.set(1, 32);
    return (a | b).at(1) == 42;                          // ...is NOT consulted
}
static_assert(target_strategies_decide_the_fold());

// The trait's default is Replace until configured — value-type agnostic.
// (MergeDefaultStrategy<T>'s arithmetic-Sum rule belongs to DynamicMergeMap;
// the trait makes accumulation an explicit choice, and this proof pins that.)
consteval bool trait_default_is_replace_not_type_derived() {
    M a;
    a.set(1, 10);
    M b;
    b.set(1, 5);
    return (a | b).at(1) == 5;                           // Replace, though int
}
static_assert(trait_default_is_replace_not_type_derived());

consteval bool fold_is_frozen_and_sides_are_untouched() {
    M a;
    a.set_default_strategy(MergeStrategy::Sum);
    a.set(1, 10);
    M b;
    b.set(1, 5);
    b.set(2, 20);
    auto folded = a | b;
    static_assert(std::is_same_v<decltype(folded), Frozen>);
    return folded.at(1) == 15 && folded.at(2) == 20 &&
           a.size() == 1 && a.at(1) == 10 && b.size() == 2;
}
static_assert(fold_is_frozen_and_sides_are_untouched());

consteval bool empty_map_is_the_fold_identity() {
    M a;
    a.set(1, 10);
    const M empty;
    return (a | empty).at(1) == 10 && (empty | a).at(1) == 10 &&
           (a | empty).size() == 1 && (empty | a).size() == 1;
}
static_assert(empty_map_is_the_fold_identity());

consteval bool self_merge_is_safe_and_doubles_under_sum() {
    M m;
    m.set_default_strategy(MergeStrategy::Sum);
    m.set(1, 10);
    m.set(2, 20);
    const auto doubled = m | m;                          // functional self-merge
    m.merge_with(m);                                     // in-place self-merge (aliasing!)
    return doubled.at(1) == 20 && doubled.at(2) == 40 &&
           m.at(1) == 20 && m.at(2) == 40 && m.size() == 2;
}
static_assert(self_merge_is_safe_and_doubles_under_sum());

consteval bool replace_folds_are_order_dependent_sum_folds_are_not() {
    M base;                                              // Replace default (unset ints? no:
    base.set_default_strategy(MergeStrategy::Replace);   //  make it explicit)
    base.set(1, 1);
    M x;
    x.set(1, 2);
    M y;
    y.set(1, 3);

    M bxy = base; bxy.merge_with(x); bxy.merge_with(y);  // last writer wins: 3
    M byx = base; byx.merge_with(y); byx.merge_with(x);  // last writer wins: 2

    M s1; s1.set_default_strategy(MergeStrategy::Sum); s1.set(1, 1);
    M s2 = s1;
    s1.merge_with(x); s1.merge_with(y);                  // 1+2+3
    s2.merge_with(y); s2.merge_with(x);                  // 1+3+2
    return bxy.at(1) == 3 && byx.at(1) == 2 && s1.at(1) == 6 && s2.at(1) == 6;
}
static_assert(replace_folds_are_order_dependent_sum_folds_are_not());

// The character-sheet fold-left idiom in C++: in-place accumulation.
consteval bool character_assembly_folds_left_with_mixed_strategies() {
    M sheet;                                  // keys: 1=hp(Sum) 2=speed(Max) 3=gold(Sum)
    sheet.set_default_strategy(MergeStrategy::Sum);
    sheet.set_merge_strategy(2, MergeStrategy::Max);
    M base;  base.set(1, 10); base.set(2, 30); base.set(3, 15);
    M race;  race.set(1, 2);  race.set(2, 35);
    M cls;   cls.set(1, 8);   cls.set(3, 10);
    sheet.merge_with(base);
    sheet.merge_with(race);
    sheet.merge_with(cls);
    return sheet.at(1) == 20 && sheet.at(2) == 35 && sheet.at(3) == 25;
}
static_assert(character_assembly_folds_left_with_mixed_strategies());

// Frozen results end the C++ | chain BY DESIGN: continued folding is the
// mutable idiom (merge_with) — a frozen lhs simply has no operator|.
template <typename L, typename R>
concept can_pipe = requires(const L& l, const R& r) { l | r; };
static_assert(can_pipe<M, M>);
static_assert(can_pipe<M, gimmap<FS, mutable_trait>>);   // rhs needs only items()
static_assert(!can_pipe<Frozen, M>);

// ════════════════════════════════════════════════════════════════════════════
// 5. LAYERED MERGE — capture-time strategies, base-first fold, undo
// ════════════════════════════════════════════════════════════════════════════

using Sheet = gimmap<FS, mutable_trait, merge_trait<int>, layer_trait<int, int>>;

consteval bool contribution_strategy_is_captured_at_apply_time() {
    Sheet s;
    s.set(1, 10);
    s.set_default_strategy(MergeStrategy::Sum);
    s.apply("race", 1, 5);                               // captured: Sum
    s.set_default_strategy(MergeStrategy::Multiply);     // later change...
    return s.observe(1) == 15;                           // ...does not re-fold history
}
static_assert(contribution_strategy_is_captured_at_apply_time());

consteval bool base_channel_edits_re_fold_under_existing_layers() {
    Sheet s;
    s.set(1, 30);
    s.apply("haste", 1, MergeStrategy::Multiply, 2);
    const bool before = s.observe(1) == 60;
    s.set(1, 40);                                        // base written AFTER the layer
    return before && s.observe(1) == 80;                 // fold is base-first, live
}
static_assert(base_channel_edits_re_fold_under_existing_layers());

consteval bool removing_one_source_keeps_the_others() {
    Sheet s;
    s.set(1, 10);
    s.apply("race", 1, MergeStrategy::Sum, 2);
    s.apply("item", 1, MergeStrategy::Sum, 4);
    s.apply("curse", 1, MergeStrategy::Multiply, 0);
    const bool cursed = s.observe(1) == 0;
    s.remove("curse");
    const bool lifted = s.observe(1) == 16;              // race + item intact
    s.remove("race");
    return cursed && lifted && s.observe(1) == 14;       // base + item
}
static_assert(removing_one_source_keeps_the_others());

consteval bool repeated_same_source_applies_stack_but_footprint_lists_once() {
    Sheet s;
    s.apply("blessing", 7, MergeStrategy::Sum, 5);
    s.apply("blessing", 7, MergeStrategy::Sum, 3);
    const bool stacked = s.observe(7) == 8 && s.sources(7).size() == 2;
    const bool once = s.footprint("blessing").size() == 1;
    s.remove("blessing");                                // one removal drops BOTH
    return stacked && once && !s.holds(7);
}
static_assert(repeated_same_source_applies_stack_but_footprint_lists_once());

consteval bool merge_in_writes_base_under_layers() {
    Sheet s;
    s.set_default_strategy(MergeStrategy::Sum);
    s.merge_in(1, 10);                                   // base: 10
    s.apply("race", 1, 5);                               // layer: +5 (Sum captured)
    s.merge_in(1, 7);                                    // base folds: 17
    return s.observe(1) == 22 && s.snapshot().at(1) == 22;
}
static_assert(merge_in_writes_base_under_layers());

// ════════════════════════════════════════════════════════════════════════════
// 6. HOOK INTERPLAY — merges are writes, so hooks observe them
// ════════════════════════════════════════════════════════════════════════════

struct CountingHook {
    int* count;
    constexpr void operator()(const int&, const int&) const { ++*count; }
};

consteval bool merge_in_and_merge_with_fire_register_hooks() {
    int fired = 0;
    gimmap<FS, mutable_trait, merge_trait<int>, hooks_trait<int, int, CountingHook>> m;
    m.add_on_register(CountingHook{&fired});
    m.set_default_strategy(MergeStrategy::Sum);
    m.merge_in(1, 10);                                   // insert path: 1 event
    m.merge_in(1, 5);                                    // fold path:   1 event
    M other;
    other.set(1, 1);
    other.set(2, 2);
    m.merge_with(other);                                 // 2 more events
    return fired == 4 && m.at(1) == 16 && m.at(2) == 2;
}
static_assert(merge_in_and_merge_with_fire_register_hooks());

#endif  // PYGIM_HAS_DEDUCING_THIS

}  // namespace
