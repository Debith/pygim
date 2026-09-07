#pragma once
// mapping/id_set.h — a set of dense 32-bit ids: insertion order plus a bitmap.
//
// CORE layer: pybind-free, constexpr throughout. The result type for anything
// that answers with rows of a table (trie.h rows, interner ids, registry
// indices): membership is one bit, iteration is the members vector in the
// order they were noted, and the algebra between two sets over the SAME id
// space is a pass over one members list against the other's bitmap — a few
// nanoseconds per element. Ids are never removed: a set is built, not edited.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pygim::mapping {

class id_set {
public:
    using id_type = std::uint32_t;

    constexpr id_set() = default;

    // Adds `id`; false when it was already a member.
    constexpr bool note(id_type id) {
        const std::size_t w = id / 64;
        if (w >= m_bits.size()) m_bits.resize(w + 1, 0);
        const std::uint64_t bit = std::uint64_t{1} << (id % 64);
        if (m_bits[w] & bit) return false;
        m_bits[w] |= bit;
        m_members.push_back(id);
        return true;
    }
    [[nodiscard]] constexpr bool has(id_type id) const noexcept {
        const std::size_t w = id / 64;
        return w < m_bits.size() && ((m_bits[w] >> (id % 64)) & 1u);
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_members.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_members.empty(); }
    [[nodiscard]] constexpr const std::vector<id_type>& members() const noexcept { return m_members; }
    [[nodiscard]] constexpr id_type operator[](std::size_t i) const noexcept { return m_members[i]; }

    // A set over the same id space, pre-sized for the same bitmap width.
    [[nodiscard]] constexpr id_set sibling() const {
        id_set out;
        out.m_bits.resize(m_bits.size(), 0);
        return out;
    }
    // The members satisfying pred(id), in order.
    template <class Pred>
    [[nodiscard]] constexpr id_set where(Pred pred) const {
        id_set out = sibling();
        for (const id_type id : m_members) {
            if (pred(id)) out.note(id);
        }
        return out;
    }
    // Algebra over one id space: mine first, then theirs.
    [[nodiscard]] constexpr id_set united(const id_set& o) const {
        id_set out = m_bits.size() >= o.m_bits.size() ? sibling() : o.sibling();
        for (const id_type id : m_members) out.note(id);
        for (const id_type id : o.m_members) out.note(id);
        return out;
    }
    [[nodiscard]] constexpr id_set intersected(const id_set& o) const {
        return where([&](id_type id) { return o.has(id); });
    }
    [[nodiscard]] constexpr id_set subtracted(const id_set& o) const {
        return where([&](id_type id) { return !o.has(id); });
    }

    constexpr void reserve(std::size_t members) { m_members.reserve(members); }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_members.capacity() * sizeof(id_type) + m_bits.capacity() * sizeof(std::uint64_t);
    }

private:
    std::vector<id_type> m_members;    // insertion order
    std::vector<std::uint64_t> m_bits; // membership per id
};

}  // namespace pygim::mapping
