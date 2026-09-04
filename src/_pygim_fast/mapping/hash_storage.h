#pragma once
// mapping/hash_storage.h — the hashed storage engine.
//
// CORE layer: pybind-free. The runtime counterpart of flat_storage (storage.h):
// std::unordered_map behind the same `storage` concept, for registries that
// accumulate entries while the program runs and may grow large — O(1) expected
// insert and lookup, no ordering guarantee (`ordered = false`, and that is
// part of the contract). Not usable in constant evaluation: the library map
// has no constexpr support on any compiler in the build matrix (P3372 is
// C++26 on paper only), which is exactly why flat_storage exists.

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "storage.h"

namespace pygim::mapping {

template <typename K, typename V, typename Hash = std::hash<K>, typename Eq = std::equal_to<K>>
class hash_storage {
public:
    using key_type = K;
    using mapped_type = V;
    using item_type = std::pair<const K, V>;

    static constexpr bool ordered = false;   // items() order is unspecified

    hash_storage() = default;

    [[nodiscard]] V* find(const K& key) {
        auto it = m_map.find(key);
        return it == m_map.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const V* find(const K& key) const {
        auto it = m_map.find(key);
        return it == m_map.end() ? nullptr : &it->second;
    }
    void insert(const K& key, V value) { m_map.insert_or_assign(key, std::move(value)); }
    bool erase(const K& key) { return m_map.erase(key) > 0; }
    [[nodiscard]] const std::unordered_map<K, V, Hash, Eq>& items() const noexcept { return m_map; }
    [[nodiscard]] std::size_t size() const noexcept { return m_map.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_map.empty(); }
    void clear() { m_map.clear(); }
    void reserve(std::size_t capacity) { m_map.reserve(capacity); }

private:
    std::unordered_map<K, V, Hash, Eq> m_map;
};

static_assert(storage<hash_storage<int, int>>);
static_assert(!hash_storage<int, int>::ordered);

}  // namespace pygim::mapping
