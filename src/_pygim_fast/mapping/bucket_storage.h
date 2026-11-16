#pragma once
// mapping/bucket_storage.h — the bucketed storage engine: short ASCII keys, found by their first
// byte and their length.
//
// CORE layer: pybind-free, a literal type, constexpr throughout. The engine of a compile-time
// registry read on a hot path — a reader's keywords, its punctuators — where flat_storage's binary
// search compares a word with about seven keys and this one with the few that share its first
// byte and length (at most five among C++'s 96 keywords: `c` and 8 bytes). A key absent from the
// table costs one bucket read when nothing shares those two.
//
//     bucket_storage<int, 8> s;   s.insert("for", 1);   s.insert("if", 2);
//     *s.find("if") -> 2          s.find("in") -> nullptr      (bucket ('i', 2): "if" only)
//
// Keys are views: their text must outlive the table (string literals do). A key is 1 to 16 bytes
// whose first byte is ASCII; insert() refuses another (std::logic_error, a build error in constant
// evaluation), and so does a full bucket or table. find() of such a key finds nothing. items()
// iterates in insertion order, which is deterministic (`ordered`), until an erase() moves the last
// item into the erased one's place.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "storage.h"

namespace pygim::mapping {

template <typename V, std::size_t Capacity>
class bucket_storage {
    static_assert(Capacity < 256, "a bucket names an item by one byte");

public:
    using key_type = std::string_view;
    using mapped_type = V;
    using item_type = std::pair<std::string_view, V>;

    static constexpr bool ordered = true;   // insertion order
    static constexpr std::size_t longest_key = 16;

    constexpr bucket_storage() = default;

    /// The value at `key`, or none; as const as the storage (C++23's deduced `this`: one body).
    template <class Self>
    [[nodiscard]] constexpr auto find(this Self& self, std::string_view key) noexcept -> decltype(&self.m_items[0].second) {
        const std::size_t at = self.item_of(key);
        return at != 0 ? &self.m_items[at - 1].second : nullptr;
    }
    // Whether `key` is held, and its value: an index compared, never a pointer. A sanitizer build's
    // constant evaluation (GCC, -fsanitize=undefined) refuses a pointer into a constexpr table
    // compared with nullptr, so a constexpr table answers through these.
    [[nodiscard]] constexpr bool contains(std::string_view key) const noexcept { return item_of(key) != 0; }
    [[nodiscard]] constexpr V value_or(std::string_view key, V fallback) const noexcept {
        const std::size_t at = item_of(key);
        return at != 0 ? m_items[at - 1].second : fallback;
    }
    /// Inserts `key` or assigns its value.
    constexpr void insert(std::string_view key, V value) {
        if (V* found = find(key)) {
            *found = std::move(value);
            return;
        }
        if (!fits(key)) throw std::logic_error("bucket_storage: a key is 1 to 16 bytes, its first ASCII");
        if (m_size == Capacity) throw std::logic_error("bucket_storage: full");
        bucket& b = m_buckets[bucket_of(key)];
        std::size_t k = 0;
        while (k < b.size() && b[k] != 0) ++k;
        if (k == b.size()) throw std::logic_error("bucket_storage: more keys share a first byte and a length than a bucket holds");
        m_items[m_size] = item_type(key, std::move(value));
        b[k] = static_cast<std::uint8_t>(++m_size);
    }
    constexpr bool erase(std::string_view key) {
        if (!fits(key)) return false;
        bucket& b = m_buckets[bucket_of(key)];
        std::size_t k = 0;
        while (k < b.size() && b[k] != 0 && m_items[b[k] - 1].first != key) ++k;
        if (k == b.size() || b[k] == 0) return false;
        const std::uint8_t gone = b[k];
        for (; k + 1 < b.size() && b[k + 1] != 0; ++k) b[k] = b[k + 1];   // the bucket closes up
        b[k] = 0;
        const auto last = static_cast<std::uint8_t>(m_size);
        if (gone != last) {   // the last item takes the erased one's place
            m_items[gone - 1] = std::move(m_items[last - 1]);
            for (std::uint8_t& i : m_buckets[bucket_of(m_items[gone - 1].first)]) {
                if (i == last) i = gone;
            }
        }
        --m_size;
        return true;
    }

    [[nodiscard]] constexpr std::span<const item_type> items() const noexcept { return {m_items.data(), m_size}; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }
    constexpr void clear() noexcept {
        m_buckets = {};
        m_size = 0;
    }

private:
    using bucket = std::array<std::uint8_t, 8>;   // the items' indices + 1; a 0 ends the bucket

    [[nodiscard]] static constexpr bool fits(std::string_view key) noexcept {
        return !key.empty() && key.size() <= longest_key && static_cast<unsigned char>(key[0]) < 128;
    }
    [[nodiscard]] static constexpr std::size_t bucket_of(std::string_view key) noexcept {
        return static_cast<unsigned char>(key[0]) * (longest_key + 1) + key.size();
    }
    // The index + 1 of `key`'s item, or 0.
    [[nodiscard]] constexpr std::size_t item_of(std::string_view key) const noexcept {
        if (!fits(key)) return 0;
        for (const std::uint8_t i : m_buckets[bucket_of(key)]) {
            if (i == 0 || m_items[i - 1].first == key) return i;
        }
        return 0;
    }

    std::array<item_type, Capacity> m_items{};
    std::size_t m_size = 0;
    std::array<bucket, 128 * (longest_key + 1)> m_buckets{};
};

static_assert(storage<bucket_storage<int, 8>>);
static_assert([] {   // the example above: a reader's keyword table
    bucket_storage<int, 8> s;
    s.insert("for", 1);
    s.insert("if", 2);
    return *s.find("if") == 2 && s.find("in") == nullptr && s.size() == 2 && s.value_or("in", 0) == 0 && s.contains("for");
}());

}  // namespace pygim::mapping
