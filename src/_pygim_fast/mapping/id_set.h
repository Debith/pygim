#pragma once
// mapping/id_set.h — a set of dense ids: insertion order plus a bitmap.
//
// CORE layer: pybind-free, constexpr throughout. The result type for anything
// that answers with rows of a table (trie.h rows, interner ids, registry
// indices): membership is one bit, iteration is the members vector in the
// order they were noted, and the algebra between two sets over the SAME id
// space is a pass over one members list against the other's bitmap — a few
// nanoseconds per element.
//
// basic_id_set<Id, Word> is generic over the id's integer type (what the
// members vector holds: 16-bit ids for a registry of a few dozen entries,
// 32-bit for table rows, 64-bit when a table hands those out) and the bitmap
// word (the machine's widest single-instruction integer — 64 bits everywhere
// the build runs, 32 where it is not). `id_set` is the toolkit's default
// instantiation, the one the trie and the interners hand ids to. Asking only for the SIZE of a union, intersection
// or difference is cheaper still: a popcount over the two bitmaps, 64 ids per
// step, with no result built (count_united & co). Ids are never removed: a
// set is built, not edited.

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace pygim::mapping {

template <std::unsigned_integral Id = std::uint32_t, std::unsigned_integral Word = std::uint64_t>
class basic_id_set {
public:
    using id_type = Id;
    using word_type = Word;
    static constexpr std::size_t word_bits = std::numeric_limits<Word>::digits;

    constexpr basic_id_set() = default;

    // Adds `id`; false when it was already a member.
    constexpr bool note(id_type id) {
        const std::size_t w = static_cast<std::size_t>(id) / word_bits;
        if (w >= m_bits.size()) m_bits.resize(w + 1, 0);
        const Word bit = static_cast<Word>(Word{1} << (static_cast<std::size_t>(id) % word_bits));
        if (m_bits[w] & bit) return false;
        m_bits[w] |= bit;
        m_members.push_back(id);
        return true;
    }
    [[nodiscard]] constexpr bool has(id_type id) const noexcept {
        const std::size_t w = static_cast<std::size_t>(id) / word_bits;
        return w < m_bits.size() && ((m_bits[w] >> (static_cast<std::size_t>(id) % word_bits)) & Word{1});
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_members.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_members.empty(); }
    [[nodiscard]] constexpr const std::vector<id_type>& members() const noexcept { return m_members; }
    [[nodiscard]] constexpr id_type operator[](std::size_t i) const noexcept { return m_members[i]; }

    // A set over the same id space, pre-sized for the same bitmap width.
    [[nodiscard]] constexpr basic_id_set sibling() const {
        basic_id_set out;
        out.m_bits.resize(m_bits.size(), 0);
        return out;
    }
    // The members satisfying pred(id), in order.
    template <class Pred>
    [[nodiscard]] constexpr basic_id_set where(Pred pred) const {
        basic_id_set out = sibling();
        for (const id_type id : m_members) {
            if (pred(id)) out.note(id);
        }
        return out;
    }
    // Algebra over one id space: mine first, then theirs.
    [[nodiscard]] constexpr basic_id_set united(const basic_id_set& o) const {
        basic_id_set out = m_bits.size() >= o.m_bits.size() ? sibling() : o.sibling();
        for (const id_type id : m_members) out.note(id);
        for (const id_type id : o.m_members) out.note(id);
        return out;
    }
    [[nodiscard]] constexpr basic_id_set intersected(const basic_id_set& o) const {
        return where([&](id_type id) { return o.has(id); });
    }
    [[nodiscard]] constexpr basic_id_set subtracted(const basic_id_set& o) const {
        return where([&](id_type id) { return !o.has(id); });
    }

    // |this ∪ o|, |this ∩ o|, |this ∖ o| without building the result: one
    // popcount per 64-bit word of the bitmaps (a missing word is all zeros).
    [[nodiscard]] constexpr std::size_t count_united(const basic_id_set& o) const noexcept {
        const std::size_t common = m_bits.size() < o.m_bits.size() ? m_bits.size() : o.m_bits.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < common; ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i] | o.m_bits[i]));
        for (std::size_t i = common; i < m_bits.size(); ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i]));
        for (std::size_t i = common; i < o.m_bits.size(); ++i) n += static_cast<std::size_t>(std::popcount(o.m_bits[i]));
        return n;
    }
    [[nodiscard]] constexpr std::size_t count_intersected(const basic_id_set& o) const noexcept {
        const std::size_t common = m_bits.size() < o.m_bits.size() ? m_bits.size() : o.m_bits.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < common; ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i] & o.m_bits[i]));
        return n;
    }
    [[nodiscard]] constexpr std::size_t count_subtracted(const basic_id_set& o) const noexcept {
        const std::size_t common = m_bits.size() < o.m_bits.size() ? m_bits.size() : o.m_bits.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < common; ++i) n += static_cast<std::size_t>(std::popcount(static_cast<Word>(m_bits[i] & ~o.m_bits[i])));
        for (std::size_t i = common; i < m_bits.size(); ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i]));
        return n;
    }

    constexpr void reserve(std::size_t members) { m_members.reserve(members); }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_members.capacity() * sizeof(id_type) + m_bits.capacity() * sizeof(Word);
    }

private:
    std::vector<id_type> m_members;    // insertion order
    std::vector<Word> m_bits;         // membership per id, word_bits ids per word
};

// The default: 32-bit ids (trie rows, interner ids) in 64-bit words.
using id_set = basic_id_set<>;

}  // namespace pygim::mapping
