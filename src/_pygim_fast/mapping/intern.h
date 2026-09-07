#pragma once
// mapping/intern.h — the interner concept and its two engines.
//
// CORE layer: pybind-free, constexpr throughout. An interner is the sibling
// of `storage` (storage.h) for a different shape of table: it OWNS the bytes
// of every distinct key once (an arena) and ASSIGNS the value — the id is the
// insertion index — so it is a bijection (bytes -> id, id -> bytes), it is
// looked up by bytes that may live anywhere (heterogeneous by nature), and it
// is append-only: an id never moves, which is what lets a row elsewhere hold
// one. Those three properties are exactly what the storage concept cannot
// express (owned-key find, caller-supplied value, mandatory erase), so this is
// a second concept in the toolkit rather than a storage engine.
//
// Engines, mirroring flat_storage / hash_storage:
//   * flat_interner   — a sorted index of ids, binary search, no hashing:
//                       for small tables (names, extensions) and constant
//                       evaluation.
//   * hashed_interner — open addressing (linear probing, load <= 1/2) over
//                       the arena: O(1) expected, for tables that grow while
//                       the program runs. Also usable in constant evaluation
//                       (every piece is constexpr), so proofs may use either.
// The laws both must satisfy are in tests/static/mapping_proofs.cpp.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../utils/hash.h"

namespace pygim::mapping {

template <class I>
concept interner = std::movable<I> && requires(I i, const I ci, std::string_view s, typename I::id_type id, std::size_t n) {
    typename I::id_type;
    { I::npos } -> std::convertible_to<typename I::id_type>;
    { I::hashed } -> std::convertible_to<bool>;
    { i.intern(s) } -> std::same_as<typename I::id_type>;   // find-or-add; id == insertion order
    { ci.find(s) } -> std::same_as<typename I::id_type>;    // npos when absent
    { ci[id] } -> std::same_as<std::string_view>;           // the bytes of an id
    { ci.size() } -> std::convertible_to<std::size_t>;
    { ci.bytes() } -> std::convertible_to<std::size_t>;
    i.reserve(n, n);                                        // (ids, text bytes)
};

// The arena both engines share: every distinct string once, contiguous, with
// offsets — id -> string_view is two loads.
class intern_arena {
public:
    using id_type = std::uint32_t;
    static constexpr id_type npos = std::numeric_limits<id_type>::max();

    constexpr intern_arena() : m_offsets{0} {}

    [[nodiscard]] constexpr std::string_view operator[](id_type id) const noexcept {
        return {m_text.data() + m_offsets[id], static_cast<std::size_t>(m_offsets[id + 1] - m_offsets[id])};
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_offsets.size() - 1; }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_text.capacity() + m_offsets.capacity() * sizeof(std::uint64_t);
    }
    constexpr void reserve(std::size_t ids, std::size_t text_bytes) {
        m_offsets.reserve(ids + 1);
        if (text_bytes) m_text.reserve(text_bytes);
    }

protected:
    constexpr id_type append(std::string_view s) {
        const auto id = static_cast<id_type>(size());
        m_text.append(s);
        m_offsets.push_back(static_cast<std::uint64_t>(m_text.size()));
        return id;
    }

private:
    std::string m_text;
    std::vector<std::uint64_t> m_offsets;
};

// ── flat: a sorted index of ids ──────────────────────────────────────────────
class flat_interner : public intern_arena {
public:
    static constexpr bool hashed = false;

    constexpr flat_interner() = default;

    [[nodiscard]] constexpr id_type intern(std::string_view s) {
        const auto pos = lower_bound(s);
        if (pos != m_sorted.end() && (*this)[*pos] == s) return *pos;
        const id_type id = append(s);
        m_sorted.insert(m_sorted.begin() + (pos - m_sorted.begin()), id);
        return id;
    }
    [[nodiscard]] constexpr id_type find(std::string_view s) const noexcept {
        const auto pos = lower_bound(s);
        return pos != m_sorted.end() && (*this)[*pos] == s ? *pos : npos;
    }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return intern_arena::bytes() + m_sorted.capacity() * sizeof(id_type);
    }
    constexpr void reserve(std::size_t ids, std::size_t text_bytes) {
        intern_arena::reserve(ids, text_bytes);
        m_sorted.reserve(ids);
    }

private:
    using index_iter = std::vector<id_type>::iterator;
    using index_citer = std::vector<id_type>::const_iterator;
    [[nodiscard]] constexpr index_citer lower_bound(std::string_view s) const noexcept {
        return std::lower_bound(m_sorted.begin(), m_sorted.end(), s,
                                [this](id_type id, std::string_view key) { return (*this)[id] < key; });
    }
    [[nodiscard]] constexpr index_iter lower_bound(std::string_view s) noexcept {
        return std::lower_bound(m_sorted.begin(), m_sorted.end(), s,
                                [this](id_type id, std::string_view key) { return (*this)[id] < key; });
    }

    std::vector<id_type> m_sorted;   // ids ordered by their text
};

// ── hashed: open addressing over the arena ───────────────────────────────────
class hashed_interner : public intern_arena {
public:
    static constexpr bool hashed = true;

    constexpr hashed_interner() : m_slots(64, 0) {}

    [[nodiscard]] constexpr id_type intern(std::string_view s) {
        const std::size_t i = probe(s);
        if (m_slots[i]) return m_slots[i] - 1;
        const id_type id = append(s);
        m_slots[i] = id + 1;
        if (size() * 2 > m_slots.size()) grow(m_slots.size() * 2);
        return id;
    }
    [[nodiscard]] constexpr id_type find(std::string_view s) const noexcept {
        const id_type slot = m_slots[probe(s)];
        return slot ? slot - 1 : npos;
    }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return intern_arena::bytes() + m_slots.capacity() * sizeof(id_type);
    }
    constexpr void reserve(std::size_t ids, std::size_t text_bytes) {
        intern_arena::reserve(ids, text_bytes);
        if (ids * 2 > m_slots.size()) grow(hash::slots_for(ids));
    }

private:
    [[nodiscard]] constexpr std::size_t probe(std::string_view s) const noexcept {
        const std::size_t mask = m_slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(hash::mix64(hash::fnv1a(s))) & mask;
        while (m_slots[i] && (*this)[m_slots[i] - 1] != s) i = (i + 1) & mask;
        return i;
    }
    constexpr void grow(std::size_t new_size) {
        std::vector<id_type> slots(new_size, 0);
        const std::size_t mask = slots.size() - 1;
        for (id_type id = 0; id < size(); ++id) {
            std::size_t i = static_cast<std::size_t>(hash::mix64(hash::fnv1a((*this)[id]))) & mask;
            while (slots[i]) i = (i + 1) & mask;
            slots[i] = id + 1;
        }
        m_slots.swap(slots);
    }

    std::vector<id_type> m_slots;   // 0 = empty, else id + 1
};

static_assert(interner<flat_interner>);
static_assert(interner<hashed_interner>);

}  // namespace pygim::mapping
