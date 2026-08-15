#pragma once
// mapping/merge.h — the merge strategy machinery: enum, resolution, combine.
//
// CORE layer: pybind-free. Whole-map merge is an OPERATION, not a product:
// merged()/operator| fold immediately and return the FROZEN base map — a
// merged result IS a snapshot. What keeps fold-in-place merging alive as its
// own capability is unbounded accumulation (QuickTimer's PhaseMap: thousands
// of Sum merges into the same keys — layers would store every contribution;
// merge_in folds to one value). Guidance: assembly = layered (step 4),
// accumulation = merge-in-place.
//
// The trait owns RESOLUTION (which strategy applies: per-key table, then the
// default; the merge TARGET decides). APPLICATION (how two values combine) is
// the ADL customization point `merge_combine(strategy, lhs, rhs)`: this
// header provides the arithmetic overload; the Python adapter provides the
// py::object overload (PyNumber_Add, deep-dict recursion, list union). One
// resolution semantics, two value domains.

#include <type_traits>
#include <utility>
#include <vector>

#include "gimmap.h"
#include "storage.h"

namespace pygim::mapping {

enum class MergeStrategy {
    Sum,
    Max,
    Min,
    Replace,
    Extend,    // sequences concatenate (arithmetic degrades to Sum)
    Union,     // sequences append only unseen elements (order-preserving)
    Deep,      // mappings merge recursively, leaves resolved by their own strategy
    Multiply,  // numeric product — rider effects like "Speed x0" (non-arithmetic: replace)
};

template <typename T, typename = void>
struct MergeDefaultStrategy {
    static constexpr MergeStrategy value = MergeStrategy::Replace;
};

template <typename T>
struct MergeDefaultStrategy<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
    static constexpr MergeStrategy value = MergeStrategy::Sum;
};

// Application for plain value types. Extend/Union/Deep concern containers;
// this generic overload resolves them to the closest scalar meaning (Extend
// sums arithmetics, Union/Deep replace) — full container semantics live in
// the value-domain overloads (the Python adapter's py::object one).
template <typename T>
[[nodiscard]] constexpr T merge_combine(MergeStrategy strategy, const T& lhs, const T& rhs) {
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
            if constexpr (std::is_arithmetic_v<T>) return lhs > rhs ? lhs : rhs;
            return rhs;
        case MergeStrategy::Min:
            if constexpr (std::is_arithmetic_v<T>) return lhs < rhs ? lhs : rhs;
            return rhs;
    }
    return rhs;
}

#if PYGIM_HAS_DEDUCING_THIS

// (merge_trait_tag lives in gimmap.h with the other family tags, so the
// dependency gate compiles on every frontend.)

// merge_trait<K> — stateful (per-key strategy table + default), hence keyed.
// The map it is mixed into supplies values and storage; the trait supplies
// the strategy bookkeeping and the fold surfaces.
template <typename K>
struct merge_trait : merge_trait_tag {
    // ── resolution state (the merge target decides) ─────────────────────────
    constexpr void set_merge_strategy(const K& key, MergeStrategy strategy) {
        m_strategies.insert(key, strategy);
    }
    constexpr void set_default_strategy(MergeStrategy strategy) noexcept {
        m_default_strategy = strategy;
    }
    [[nodiscard]] constexpr MergeStrategy default_strategy() const noexcept {
        return m_default_strategy;
    }
    [[nodiscard]] constexpr MergeStrategy strategy_for(const K& key) const {
        const MergeStrategy* strategy = m_strategies.find(key);
        return strategy == nullptr ? m_default_strategy : *strategy;
    }
    // Whether a PER-KEY strategy is set (nullptr: only the default applies).
    // Richer value domains (the Python adapter's type-based rules) slot their
    // resolution between "per-key" and "default", so they need the distinction.
    [[nodiscard]] constexpr const MergeStrategy* key_strategy(const K& key) const {
        return m_strategies.find(key);
    }
    // The whole per-key table (iterable) — carried across freeze/thaw so
    // merge configuration survives type transitions.
    [[nodiscard]] constexpr const flat_storage<K, MergeStrategy>& key_strategies() const {
        return m_strategies;
    }

    // ── fold surfaces ───────────────────────────────────────────────────────
    // merged()/operator| — merge as an operation: fold into a NEW map and
    // return it FROZEN. No mutation of either side; a merged result is a
    // snapshot, so there is no separate "merged map" product type.
    template <typename Self, typename Other>
    [[nodiscard]] constexpr auto merged(this const Self& self, const Other& other) {
        auto out = self.storage();                    // copy of the engine
        for (const auto& [key, value] : other.items()) {
            if (auto* existing = out.find(key)) {
                *existing = merge_combine(self.strategy_for(key), *existing, value);
            } else {
                out.insert(key, value);
            }
        }
        return gimmap<std::remove_cvref_t<decltype(out)>>(std::move(out));
    }

    // merge_in()/merge_with() — the in-place accumulator surface; only a
    // mutable map can fold into itself. Both routes go through set(), so
    // merges ARE writes: hooks fire, and no reference into storage is held
    // across merge_combine (whose T-supplied operators may run arbitrary
    // code — including code that mutates this very map).
    template <typename Self>
    constexpr void merge_in(this Self& self, const K& key, const auto& value)
        requires std::derived_from<Self, mutable_trait>
    {
        if (const auto* existing = self.storage().find(key)) {
            auto current = *existing;                 // own a copy; drop the pointer
            self.set(key, merge_combine(self.strategy_for(key), current, value));
        } else {
            self.set(key, value);
        }
    }
    template <typename Self, typename Other>
    constexpr void merge_with(this Self& self, const Other& other)
        requires std::derived_from<Self, mutable_trait>
    {
        // Direct iteration is safe even when &other == &self: every key of
        // self exists in self, so merge_in can only take the ASSIGN path
        // (no insert, no reallocation, references stay valid). If merge_in
        // ever grows an invalidating path, the consteval self-merge proof in
        // tests/static/mapping_merge_proofs.cpp fails the build — constant
        // evaluation rejects reads through invalidated storage.
        for (const auto& [key, value] : other.items()) self.merge_in(key, value);
    }

private:
    flat_storage<K, MergeStrategy> m_strategies{};
    MergeStrategy m_default_strategy = MergeStrategy::Replace;
};

template <typename M>
concept has_merge = std::derived_from<M, merge_trait_tag>;

// a | b — found by ADL for any merge-bearing gimmap; returns the frozen fold.
template <has_merge L, typename R>
[[nodiscard]] constexpr auto operator|(const L& lhs, const R& rhs) {
    return lhs.merged(rhs);
}

#endif  // PYGIM_HAS_DEDUCING_THIS

}  // namespace pygim::mapping
