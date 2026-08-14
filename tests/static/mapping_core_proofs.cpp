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

#include "../../src/_pygim_fast/mapping/gimmap.h"

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

#endif  // PYGIM_HAS_DEDUCING_THIS

}  // namespace
