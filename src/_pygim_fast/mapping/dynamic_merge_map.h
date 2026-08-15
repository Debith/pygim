#pragma once

// DynamicMergeMap — a mapping that is never collapsed until observed.
//
// Every key holds two channels:
//
//  * base   — the map's own value (set / merge_in). Folded in place exactly like
//             the pre-layered class, so bookkeeping users (QuickTimer's PhaseMap)
//             behave identically.
//  * layers — source-tagged contributions (apply / merge_with(other, source) /
//             operator<<). Each contribution records WHO wrote it and HOW it
//             folds; contributions stay separate, so removing a source
//             (remove / operator>>) simply drops its entries and the next
//             observation re-folds the survivors — reversible without inverses
//             (a "speed x0" condition can be lifted and the old speed returns).
//
// A source -> keys reverse index (the source's *footprint*) is maintained at
// apply time, so locating or removing everything one cause contributed is
// O(footprint of that source), never a scan of the whole map.
//
// observe(key) folds base then layers in application order; snapshot() folds the
// whole map into a NEW plain, key-sorted mapping — a frozen point-in-time view
// independent of later edits. `species << paralyzed` merges a whole map in under
// its id(); `species >> "paralyzed"` (or >> paralyzed) removes it again.
//
// Everything is constexpr. The compile_tests namespace at the bottom proves the
// reversibility, precedence, and footprint rules with static_asserts in every
// translation unit that includes this header: a wrong rule fails the build.
//
// Storage is a small sorted flat map (deterministic iteration order). Its
// standard successor is std::flat_map, but that header is absent before GCC 15
// and constexpr only on GCC 16 trunk — the portable store keeps these proofs
// running under every compiler that builds pygim, and swapping later is local.

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// MergeStrategy and MergeDefaultStrategy moved to mapping/merge.h (toolkit
// step 2); same names, same namespace — the public shape is unchanged. This
// class's internal combine() stays until step 4 rebases the layered channel
// onto the toolkit's layer_trait.
#include "merge.h"

namespace pygim::mapping {

namespace detail {

// A minimal sorted flat map: contiguous, deterministic iteration, constexpr on
// every supported toolchain. Internal detail of DynamicMergeMap only.
template <typename K, typename V>
class FlatMap {
public:
    using Item = std::pair<K, V>;

    [[nodiscard]] constexpr V* find(const K& key) {
        const std::size_t idx = lower_bound_idx(key);
        if (idx < m_items.size() && m_items[idx].first == key) return &m_items[idx].second;
        return nullptr;
    }
    [[nodiscard]] constexpr const V* find(const K& key) const {
        const std::size_t idx = lower_bound_idx(key);
        if (idx < m_items.size() && m_items[idx].first == key) return &m_items[idx].second;
        return nullptr;
    }
    [[nodiscard]] constexpr V& operator[](const K& key) {
        const std::size_t idx = lower_bound_idx(key);
        if (idx < m_items.size() && m_items[idx].first == key) return m_items[idx].second;
        return m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(idx), {key, V{}})
            ->second;
    }
    constexpr bool erase(const K& key) {
        const std::size_t idx = lower_bound_idx(key);
        if (idx >= m_items.size() || !(m_items[idx].first == key)) return false;
        m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(idx));
        return true;
    }
    [[nodiscard]] constexpr const std::vector<Item>& items() const noexcept { return m_items; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_items.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_items.empty(); }
    constexpr void clear() { m_items.clear(); }

private:
    [[nodiscard]] constexpr std::size_t lower_bound_idx(const K& key) const {
        std::size_t lo = 0;
        std::size_t hi = m_items.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (m_items[mid].first < key) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    std::vector<Item> m_items{};
};

}  // namespace detail

template <typename Key, typename T, typename SourceId = std::string>
class DynamicMergeMap {
public:
    using key_type = Key;
    using mapped_type = T;
    using source_type = SourceId;

    // One layered contribution: who wrote it, how it folds, what it adds.
    struct Contribution {
        SourceId source;
        MergeStrategy strategy;
        T value;
    };

    constexpr explicit DynamicMergeMap(
        MergeStrategy default_strategy = MergeDefaultStrategy<T>::value)
        : m_default_strategy(default_strategy) {}

    constexpr DynamicMergeMap(std::initializer_list<std::pair<Key, T>> values,
                              MergeStrategy default_strategy = MergeDefaultStrategy<T>::value)
        : m_default_strategy(default_strategy) {
        for (const auto& [key, value] : values) set(key, value);
    }

    // -- identity: names this map as a source when merged into another --------
    // `species << paralyzed` files paralyzed's values under paralyzed.id(); an
    // unnamed map merges into the base channel instead (plain, non-removable).
    [[nodiscard]] constexpr const SourceId& id() const noexcept { return m_id; }
    constexpr void set_id(SourceId id) { m_id = std::move(id); }

    // -- strategies ------------------------------------------------------------
    constexpr void set_default_strategy(MergeStrategy strategy) noexcept {
        m_default_strategy = strategy;
    }
    [[nodiscard]] constexpr MergeStrategy default_strategy() const noexcept {
        return m_default_strategy;
    }
    constexpr void set_merge_strategy(const Key& key, MergeStrategy strategy) {
        m_strategies[key] = strategy;
    }

    // -- base channel (the map's own values; folded in place, classic behaviour) --
    constexpr void set(const Key& key, const T& value) {
        KeyState& state = m_state[key];
        state.has_base = true;
        state.base = value;
    }
    constexpr void merge_in(const Key& key, const T& rhs) {
        KeyState& state = m_state[key];
        if (!state.has_base) {
            state.has_base = true;
            state.base = rhs;
            return;
        }
        state.base = combine(strategy_for(key), state.base, rhs);
    }
    // Classic whole-map merge: fold other's observed items into the base channel
    // through THIS map's strategies (the pre-layered merge_with / operator|).
    constexpr void merge_with(const DynamicMergeMap& other) {
        for (const auto& [key, value] : other.snapshot()) merge_in(key, value);
    }
    [[nodiscard]] constexpr DynamicMergeMap merged(const DynamicMergeMap& other) const {
        DynamicMergeMap out = *this;
        out.merge_with(other);
        return out;
    }
    friend constexpr DynamicMergeMap operator|(const DynamicMergeMap& lhs,
                                               const DynamicMergeMap& rhs) {
        return lhs.merged(rhs);
    }

    // -- layered channel (source-tagged, reversible, footprint-indexed) --------
    constexpr void apply(const SourceId& source, const Key& key, const T& value) {
        apply(source, key, strategy_for(key), value);
    }
    constexpr void apply(const SourceId& source, const Key& key, MergeStrategy strategy,
                         const T& value) {
        m_state[key].layers.push_back({source, strategy, value});
        std::vector<Key>& keys = m_footprint[source];
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    }
    // Layered whole-map merge: other's observed items enter as contributions from
    // `source`, each folding by OTHER's OWN strategy — the contributor declares
    // how it combines (a Multiply-default condition map multiplies in).
    constexpr void merge_with(const DynamicMergeMap& other, const SourceId& source) {
        std::vector<Staged> incoming;   // staged first: safe even when &other == this
        for (const auto& [key, state] : other.m_state.items()) {
            incoming.push_back({key, other.strategy_for(key), other.fold(state)});
        }
        for (const Staged& item : incoming) apply(source, item.key, item.strategy, item.value);
    }
    // Drop every contribution `source` made — O(footprint), via the reverse index.
    constexpr void remove(const SourceId& source) {
        std::vector<Key>* keys = m_footprint.find(source);
        if (keys == nullptr) return;
        for (const Key& key : *keys) {
            KeyState* state = m_state.find(key);
            if (state == nullptr) continue;
            std::erase_if(state->layers,
                          [&](const Contribution& c) { return c.source == source; });
            if (!state->has_base && state->layers.empty()) m_state.erase(key);
        }
        m_footprint.erase(source);
    }

    // `species << paralyzed` — merge a named map in under its id (unnamed: base).
    friend constexpr DynamicMergeMap& operator<<(DynamicMergeMap& target,
                                                 const DynamicMergeMap& modifier) {
        if (modifier.m_id == SourceId{}) target.merge_with(modifier);
        else target.merge_with(modifier, modifier.m_id);
        return target;
    }
    // `species >> paralyzed` / `species >> "paralyzed"` — lift it back out.
    friend constexpr DynamicMergeMap& operator>>(DynamicMergeMap& target,
                                                 const DynamicMergeMap& modifier) {
        target.remove(modifier.m_id);
        return target;
    }
    friend constexpr DynamicMergeMap& operator>>(DynamicMergeMap& target,
                                                 const SourceId& source) {
        target.remove(source);
        return target;
    }

    // -- observation (collapse on demand; the live map keeps every layer) ------
    [[nodiscard]] constexpr bool contains(const Key& key) const {
        const KeyState* state = m_state.find(key);
        return state != nullptr && (state->has_base || !state->layers.empty());
    }
    [[nodiscard]] constexpr T observe(const Key& key) const {
        const KeyState* state = m_state.find(key);
        if (state == nullptr) throw std::out_of_range("DynamicMergeMap: key not found");
        return fold(*state);
    }
    [[nodiscard]] constexpr T at(const Key& key) const { return observe(key); }
    [[nodiscard]] constexpr T value_or(const Key& key, const T& fallback) const {
        const KeyState* state = m_state.find(key);
        if (state == nullptr) return fallback;
        return fold(*state);
    }
    // A NEW, frozen, key-sorted single mapping — the collapsed snapshot. Later
    // edits to the live map do not touch it (and vice versa).
    [[nodiscard]] constexpr std::vector<std::pair<Key, T>> snapshot() const {
        std::vector<std::pair<Key, T>> out;
        out.reserve(m_state.size());
        for (const auto& [key, state] : m_state.items()) out.emplace_back(key, fold(state));
        return out;
    }

    // -- provenance -------------------------------------------------------------
    // The keys `source` touches (its reverse-index entry); empty when unknown.
    [[nodiscard]] constexpr std::vector<Key> footprint(const SourceId& source) const {
        const std::vector<Key>* keys = m_footprint.find(source);
        if (keys == nullptr) return {};
        return *keys;
    }
    // The sources layered onto `key`, in application order (base channel excluded).
    [[nodiscard]] constexpr std::vector<SourceId> sources(const Key& key) const {
        std::vector<SourceId> out;
        const KeyState* state = m_state.find(key);
        if (state == nullptr) return out;
        for (const Contribution& c : state->layers) out.push_back(c.source);
        return out;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_state.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_state.empty(); }
    constexpr void clear() {
        m_state.clear();
        m_footprint.clear();
        m_strategies.clear();
    }

private:
    struct KeyState {
        bool has_base = false;
        T base{};
        std::vector<Contribution> layers{};
    };
    struct Staged {
        Key key;
        MergeStrategy strategy;
        T value;
    };

    [[nodiscard]] constexpr MergeStrategy strategy_for(const Key& key) const {
        const MergeStrategy* strategy = m_strategies.find(key);
        return strategy == nullptr ? m_default_strategy : *strategy;
    }

    // One fold step. Extend/Union/Deep concern containers; this generic template
    // resolves them to the closest scalar meaning (Extend sums arithmetics,
    // Union/Deep replace) — full container semantics live in typed wrappers
    // (the Python gimdict binding recurses into dicts and unions sequences).
    static constexpr T combine(MergeStrategy strategy, const T& lhs, const T& rhs) {
        switch (strategy) {
            case MergeStrategy::Replace:
            case MergeStrategy::Union:
            case MergeStrategy::Deep:
                return rhs;
            case MergeStrategy::Sum:
            case MergeStrategy::Extend:
                if constexpr (std::is_arithmetic_v<T>) return lhs + rhs;
                return rhs;
            case MergeStrategy::Multiply:
                if constexpr (std::is_arithmetic_v<T>) return lhs * rhs;
                return rhs;
            case MergeStrategy::Max:
                if constexpr (std::is_arithmetic_v<T>) return std::max(lhs, rhs);
                return rhs;
            case MergeStrategy::Min:
                if constexpr (std::is_arithmetic_v<T>) return std::min(lhs, rhs);
                return rhs;
        }
        throw std::runtime_error("unsupported merge strategy");
    }

    // base first, then layers in application order; the first contributor
    // establishes the value (its strategy applies only against something below).
    [[nodiscard]] constexpr T fold(const KeyState& state) const {
        T acc{};
        bool first = true;
        if (state.has_base) {
            acc = state.base;
            first = false;
        }
        for (const Contribution& c : state.layers) {
            acc = first ? c.value : combine(c.strategy, acc, c.value);
            first = false;
        }
        return acc;
    }

    detail::FlatMap<Key, KeyState> m_state{};
    detail::FlatMap<SourceId, std::vector<Key>> m_footprint{};
    detail::FlatMap<Key, MergeStrategy> m_strategies{};
    MergeStrategy m_default_strategy;
    SourceId m_id{};
};

// ---------------------------------------------------------------------------
// Compile-time proofs — these static_asserts run in EVERY translation unit that
// includes this header; a wrong merge rule fails the build itself.
// ---------------------------------------------------------------------------
namespace compile_tests {

using TestMap = DynamicMergeMap<std::string_view, int, std::string_view>;

// The acceptance flow: a species map merges in, a Multiply-by-0 condition zeroes
// the speed, and REMOVING the condition restores it — no inverse exists for *0;
// only the uncollapsed layers make this possible.
constexpr int species_speed(int step) {
    TestMap species;
    TestMap speed({{"walking", 30}, {"flying", 0}});
    species << speed;                                    // unnamed -> base channel
    TestMap paralyzed(MergeStrategy::Multiply);
    paralyzed.set_id("paralyzed");
    paralyzed.set("walking", 0);
    if (step >= 1) species << paralyzed;
    if (step >= 2) species >> paralyzed;
    return species.at("walking");
}
static_assert(species_speed(0) == 30, "species establishes the base speed");
static_assert(species_speed(1) == 0, "paralyzed multiplies it to 0");
static_assert(species_speed(2) == 30, "removing paralyzed restores 30");

// Overlapping effects, strongest takes precedence — removal recomputes the new
// strongest from the surviving layers.
constexpr int strongest(bool haste_active) {
    TestMap m;
    m.set("speed", 30);
    m.apply("boots", "speed", MergeStrategy::Max, 40);
    if (haste_active) m.apply("haste", "speed", MergeStrategy::Max, 60);
    m.apply("gone", "speed", MergeStrategy::Max, 99);
    m.remove("gone");
    return m.at("speed");
}
static_assert(strongest(true) == 60, "max(30, 40, 60)");
static_assert(strongest(false) == 40, "max(30, 40) once haste is gone");

// The classic collapsed behaviour is unchanged: merge_in folds into the base
// channel via the map's strategies (arithmetic default: Sum), operator| unions.
constexpr int classic_sum() {
    DynamicMergeMap<int, int, std::string_view> m;
    m.merge_in(7, 2);
    m.merge_in(7, 3);
    return m.at(7);
}
static_assert(classic_sum() == 5, "merge_in keeps the pre-layered Sum fold");
static_assert([] {
    DynamicMergeMap<int, int, std::string_view> a({{1, 1}});
    DynamicMergeMap<int, int, std::string_view> b({{1, 2}});
    return (a | b).at(1);
}() == 3, "operator| keeps the classic collapsed union");
static_assert([] {
    TestMap m;
    m.set_merge_strategy("hp", MergeStrategy::Max);
    m.merge_in("hp", 5);
    m.merge_in("hp", 3);
    return m.at("hp");
}() == 5, "per-key strategy override still applies");

// Source lookup is indexed: a source's footprint is known without scanning, and
// removing it clears exactly that footprint.
static_assert([] {
    TestMap m;
    m.set("ac", 15);
    m.apply("bless#1", "attack", MergeStrategy::Sum, 4);
    m.apply("bless#1", "save", MergeStrategy::Sum, 4);
    m.apply("bless#1", "save", MergeStrategy::Sum, 1);   // same key twice: one footprint entry
    const bool before = m.footprint("bless#1").size() == 2 && m.contains("attack");
    m.remove("bless#1");
    return before && m.footprint("bless#1").empty() && !m.contains("attack") &&
           m.at("ac") == 15;
}(), "footprint indexes a source's keys; remove clears exactly those");

// Provenance and the frozen snapshot.
static_assert([] {
    TestMap m;
    m.set("speed", 30);
    m.apply("boots", "speed", MergeStrategy::Max, 40);
    const std::vector<std::string_view> who = m.sources("speed");
    const auto frozen = m.snapshot();                     // folded, key-sorted copy
    m.remove("boots");
    return who.size() == 1 && who[0] == "boots" && frozen.size() == 1 &&
           frozen[0].second == 40 && m.at("speed") == 30;
}(), "sources() names the layers; snapshot() is frozen against later edits");

// Multiply / Min behave; value_or falls back; unknown-source removal is a no-op.
static_assert([] {
    TestMap m;
    m.set("x", 6);
    m.apply("half", "x", MergeStrategy::Multiply, 2);
    m.apply("cap", "x", MergeStrategy::Min, 10);
    m.remove("never-applied");
    return m.at("x") == 10 && m.value_or("missing", -1) == -1 && !m.contains("missing");
}(), "Multiply/Min fold in order; value_or and no-op removal behave");

}  // namespace compile_tests

}  // namespace pygim::mapping
