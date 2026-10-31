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
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace pygim::mapping {

template <typename S>
concept storage =
    std::movable<S> &&
    requires(S s, const S cs, const typename S::key_type& k, typename S::mapped_type v) {
        { cs.find(k) } -> std::same_as<const typename S::mapped_type*>;
        { s.find(k) } -> std::same_as<typename S::mapped_type*>;
        { cs.contains(k) } -> std::convertible_to<bool>;               // no pointer compared: a constexpr table's
        { cs.value_or(k, v) } -> std::same_as<typename S::mapped_type>;  // answers in a sanitizer build too (#287)
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

    /// The value at `key`, or none; as const as the storage (C++23's deduced `this`: one body).
    template <class Self>
    [[nodiscard]] constexpr auto find(this Self& self, const K& key) -> decltype(&self.m_items[0].second) {
        const std::size_t at = self.index_of(key);
        return at != npos ? &self.m_items[at].second : nullptr;
    }
    /// find() by a key of another type that compares with K both ways — a string_view for a
    /// std::string key — so no K is made to look one up.
    template <class Q>
        requires(!std::is_convertible_v<const Q&, const K&>) && requires(const K& k, const Q& q) {
            { k < q } -> std::convertible_to<bool>;
            { k == q } -> std::convertible_to<bool>;
        }
    [[nodiscard]] constexpr const V* find(const Q& key) const {
        const std::size_t at = index_of(key);
        return at != npos ? &m_items[at].second : nullptr;
    }
    /// Whether `key` is held, and its value or `fallback`: an index compared, never a pointer.
    [[nodiscard]] constexpr bool contains(const K& key) const { return index_of(key) != npos; }
    [[nodiscard]] constexpr V value_or(const K& key, V fallback) const {
        const std::size_t at = index_of(key);
        return at != npos ? m_items[at].second : std::move(fallback);
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
    // insert() calls on unsorted input degrade to O(n^2) tail-shifting. Sorted
    // by (key, position) with std::sort, which a constant evaluation runs too:
    // std::stable_sort is constexpr only from C++26.
    constexpr void assign_bulk(std::vector<item_type> items) {
        std::vector<std::size_t> order(items.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return items[a].first < items[b].first || (!(items[b].first < items[a].first) && a < b);
        });
        std::vector<item_type> unique;
        unique.reserve(items.size());
        for (std::size_t k = 0; k < order.size(); ++k) {
            if (k + 1 < order.size() && items[order[k + 1]].first == items[order[k]].first) {
                continue;   // superseded by a later write to the same key
            }
            unique.push_back(std::move(items[order[k]]));
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
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    // Where `key`'s item is, or npos.
    template <class Q>
    [[nodiscard]] constexpr std::size_t index_of(const Q& key) const {
        const std::size_t idx = lower_bound_idx(key);
        return idx < m_items.size() && m_items[idx].first == key ? idx : npos;
    }

    template <class Q>
    [[nodiscard]] constexpr std::size_t lower_bound_idx(const Q& key) const {
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
static_assert([] {   // a bulk load keeps the last of equal keys, in a constant evaluation too
    flat_storage<int, int> s;
    s.assign_bulk({{2, 1}, {1, 5}, {2, 7}});
    return s.size() == 2 && *s.find(2) == 7 && *s.find(1) == 5 && s.contains(1) && !s.contains(3) && s.value_or(3, -1) == -1;
}());
static_assert([] {   // a std::string key found by a view: no string made
    flat_storage<std::string, int> s;
    s.insert("osp", 1);
    const auto& cs = s;
    return cs.find(std::string_view("osp")) != nullptr && *cs.find(std::string_view("osp")) == 1 && cs.find(std::string_view("os")) == nullptr;
}());

}  // namespace pygim::mapping
