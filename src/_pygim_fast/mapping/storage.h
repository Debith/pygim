#pragma once
// mapping/storage.h — the storage concept and the flat engine.
//
// CORE layer: pybind-free. A storage engine is a plain key->value container
// satisfying the `storage` concept below; every toolkit capability (mutation,
// merging, layering — see docs/design/mapping_toolkit.md) is written against
// the concept, never against a concrete engine. Each engine also declares
// `static constexpr bool ordered`: whether items() iterates in a
// deterministic order. That guarantee is part of the engine's contract and
// surfaces verbatim in the Python docstrings — honesty over imitating dict.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace pygim::mapping {

template <typename S>
concept storage =
    std::movable<S> &&
    requires(S s, const S cs, const typename S::key_type& k, typename S::mapped_type v) {
        { cs.find(k) } -> std::same_as<const typename S::mapped_type*>;
        { s.find(k) } -> std::same_as<typename S::mapped_type*>;
        { s.insert(k, std::move(v)) };            // insert-or-assign semantics
        { s.erase(k) } -> std::same_as<bool>;     // true when something was removed
        { cs.size() } -> std::convertible_to<std::size_t>;
        { cs.empty() } -> std::convertible_to<bool>;
        { cs.items() };                           // iterable of key/value pairs
        { S::ordered } -> std::convertible_to<bool>;
        s.clear();
    };

// flat_storage — sorted contiguous key/value pairs. Deterministic (key-sorted)
// iteration, constexpr on every supported toolchain — which is what lets the
// static proof suites in tests/static/mapping_core_proofs.cpp run on this
// engine — and the expected small-n winner: a binary search over contiguous
// memory beats hashing until the benchmark-measured crossover.
//
// Extracted from DynamicMergeMap's internal FlatMap; the original stays in
// dynamic_merge_map.h until step 2 rebases DynamicMergeMap onto the toolkit.
template <typename K, typename V>
class flat_storage {
public:
    using key_type = K;
    using mapped_type = V;
    using item_type = std::pair<K, V>;

    static constexpr bool ordered = true;   // items() is key-sorted

    constexpr flat_storage() = default;
    constexpr flat_storage(std::initializer_list<item_type> items) {
        for (const auto& [key, value] : items) insert(key, value);
    }

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
    constexpr void insert(const K& key, V value) {
        const std::size_t idx = lower_bound_idx(key);
        if (idx < m_items.size() && m_items[idx].first == key) {
            m_items[idx].second = std::move(value);
            return;
        }
        m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(idx),
                       {key, std::move(value)});
    }
    constexpr bool erase(const K& key) {
        const std::size_t idx = lower_bound_idx(key);
        if (idx >= m_items.size() || !(m_items[idx].first == key)) return false;
        m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(idx));
        return true;
    }

    // Bulk load, REPLACING the contents: sort once, keep the LAST of equal
    // keys (matching repeated insert-or-assign). O(n log n) where n repeated
    // insert() calls on unsorted input degrade to O(n^2) tail-shifting.
    constexpr void assign_bulk(std::vector<item_type> items) {
        std::stable_sort(items.begin(), items.end(),
                         [](const item_type& a, const item_type& b) {
                             return a.first < b.first;
                         });
        std::vector<item_type> unique;
        unique.reserve(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i + 1 < items.size() && items[i + 1].first == items[i].first) {
                continue;   // superseded by a later write to the same key
            }
            unique.push_back(std::move(items[i]));
        }
        m_items = std::move(unique);
    }

    [[nodiscard]] constexpr const std::vector<item_type>& items() const noexcept {
        return m_items;
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_items.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_items.empty(); }
    constexpr void clear() { m_items.clear(); }

    // Constrained rather than defaulted: a defaulted constexpr == is checked
    // eagerly at class instantiation, which breaks engines whose mapped_type
    // is not equality-comparable (e.g. the layer trait's contribution lists).
    constexpr bool operator==(const flat_storage& other) const
        requires std::equality_comparable<item_type>
    {
        return m_items == other.m_items;
    }

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

    std::vector<item_type> m_items{};
};

// Spot proof only — the exhaustive storage-law suites live in
// tests/static/mapping_core_proofs.cpp, compiled by every build.
static_assert(storage<flat_storage<int, int>>);
static_assert(flat_storage<int, int>::ordered);

}  // namespace pygim::mapping
