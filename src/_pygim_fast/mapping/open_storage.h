#pragma once
// mapping/open_storage.h — the open-addressing storage engine.
//
// CORE layer: pybind-free. Hashed like hash_storage — O(1) expected insert and
// lookup, so a table built one key at a time in arbitrary order stays linear —
// and constexpr like flat_storage, so one table serves a constant evaluation
// (a static proof) and a run time alike. The two existing engines each give
// up one of these: flat_storage shifts its sorted array on every out-of-order
// insert (quadratic over n inserts), and std::unordered_map is not usable in
// constant evaluation on any compiler in the build matrix.
//
// Layout: a dense vector of items in insertion order, and a power-of-two slot
// array of indices into it, probed linearly, at most three quarters full.
// Erasing moves the last item into the hole (so items() is insertion order
// until an erase) and closes the probe sequence by backward shift, leaving no
// tombstones. The hash is the library's own (stable_hash), never std::hash,
// whose values are not constexpr and differ between standard libraries — so a
// table's contents, and its iteration order, are the same on every machine.
//
//     open_storage<std::string, int> s;
//     s.insert("usage", 0);  s.insert("usage-1", 0);
//     *s.find("usage") += 1;            // usage -> 1
//     s.erase("usage");                 // items(): {("usage-1", 0)}

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "storage.h"

namespace pygim::mapping {

/// A hash that is constexpr and the same everywhere: FNV-1a over a string
/// key's bytes; splitmix64's finaliser over an integer or an enum.
template <typename K>
struct stable_hash {
    [[nodiscard]] constexpr std::uint64_t operator()(const K& key) const noexcept
        requires(std::is_integral_v<K> || std::is_enum_v<K> || std::convertible_to<const K&, std::string_view>)
    {
        if constexpr (std::convertible_to<const K&, std::string_view>) {
            std::uint64_t h = 0xcbf29ce484222325ull;
            for (char c : std::string_view(key)) {
                h ^= static_cast<unsigned char>(c);
                h *= 0x100000001b3ull;
            }
            return h;
        } else {
            std::uint64_t z = static_cast<std::uint64_t>(key) + 0x9e3779b97f4a7c15ull;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }
    }
};

template <typename K, typename V, typename Hash = stable_hash<K>, typename Eq = std::equal_to<K>>
class open_storage {
public:
    using key_type = K;
    using mapped_type = V;
    using item_type = std::pair<K, V>;

    static constexpr bool ordered = true;   // insertion order; an erase moves the last item into the hole

    constexpr open_storage() = default;

    [[nodiscard]] constexpr V* find(const K& key) {
        const std::size_t s = slot_of(key);
        return s == npos ? nullptr : &m_items[m_slots[s]].second;
    }
    [[nodiscard]] constexpr const V* find(const K& key) const {
        const std::size_t s = slot_of(key);
        return s == npos ? nullptr : &m_items[m_slots[s]].second;
    }

    /// Insert-or-assign.
    constexpr void insert(const K& key, V value) {
        if ((m_items.size() + 1) * 4 > m_slots.size() * 3) rehash(m_slots.empty() ? 8 : m_slots.size() * 2);
        std::size_t s = home(key);
        for (; m_slots[s] != vacant; s = (s + 1) & mask()) {
            if (Eq{}(m_items[m_slots[s]].first, key)) {
                m_items[m_slots[s]].second = std::move(value);
                return;
            }
        }
        m_slots[s] = static_cast<std::uint32_t>(m_items.size());
        m_items.emplace_back(key, std::move(value));
    }

    constexpr bool erase(const K& key) {
        std::size_t hole = slot_of(key);
        if (hole == npos) return false;
        const std::uint32_t gone = m_slots[hole];
        // Backward shift: pull later entries of the probe run into the hole
        // when their home does not lie cyclically in (hole, j].
        for (std::size_t j = (hole + 1) & mask(); m_slots[j] != vacant; j = (j + 1) & mask()) {
            const std::size_t h = home(m_items[m_slots[j]].first);
            const bool stays = hole < j ? (hole < h && h <= j) : (hole < h || h <= j);
            if (!stays) {
                m_slots[hole] = m_slots[j];
                hole = j;
            }
        }
        m_slots[hole] = vacant;
        const std::uint32_t last = static_cast<std::uint32_t>(m_items.size() - 1);
        if (gone != last) {   // the last item fills the hole in the dense vector
            m_slots[slot_of(m_items[last].first)] = gone;
            m_items[gone] = std::move(m_items[last]);
        }
        m_items.pop_back();
        return true;
    }

    constexpr void reserve(std::size_t capacity) {
        std::size_t n = 8;
        while (n * 3 < capacity * 4) n *= 2;
        if (n > m_slots.size()) rehash(n);
        m_items.reserve(capacity);
    }

    [[nodiscard]] constexpr const std::vector<item_type>& items() const noexcept { return m_items; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_items.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_items.empty(); }
    constexpr void clear() {
        m_items.clear();
        m_slots.clear();
    }

private:
    static constexpr std::uint32_t vacant = 0xFFFFFFFFu;   // a slot holding no item
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    [[nodiscard]] constexpr std::size_t mask() const noexcept { return m_slots.size() - 1; }
    [[nodiscard]] constexpr std::size_t home(const K& key) const noexcept {
        return static_cast<std::size_t>(Hash{}(key)) & mask();
    }
    /// The slot holding `key`, or npos.
    [[nodiscard]] constexpr std::size_t slot_of(const K& key) const {
        if (m_slots.empty()) return npos;
        for (std::size_t s = home(key); m_slots[s] != vacant; s = (s + 1) & mask()) {
            if (Eq{}(m_items[m_slots[s]].first, key)) return s;
        }
        return npos;
    }
    constexpr void rehash(std::size_t slots) {
        m_slots.assign(slots, vacant);
        for (std::uint32_t i = 0; i < m_items.size(); ++i) {
            std::size_t s = home(m_items[i].first);
            while (m_slots[s] != vacant) s = (s + 1) & mask();
            m_slots[s] = i;
        }
    }

    std::vector<item_type> m_items{};
    std::vector<std::uint32_t> m_slots{};
};

// Spot proof only — the storage laws are proven in tests/static/registry_core_proofs.cpp.
static_assert(storage<open_storage<int, int>>);

}  // namespace pygim::mapping
