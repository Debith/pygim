#pragma once
// mapping/id_set.h — a set of dense ids: insertion order plus a bitmap.
//
// CORE layer: pybind-free, constexpr throughout. The result type for anything
// that answers with rows of a table (trie.h rows, interner ids, registry
// indices). It keeps two views of the same membership:
//
//   members  the ids in the order they were noted — what iteration yields
//            and what a Python-facing set shows;
//   bits     one bit per id — what makes `has` a single load, and what lets
//            the algebra between two sets over the SAME id space run as a pass
//            over one members list against the other's bitmap (a few ns per
//            element), or, when only the SIZE of a union / intersection /
//            difference is wanted, as a popcount over the two bitmaps — 64
//            ids per step, no result built.
//
// Ids are never removed: a set is built, not edited. Two sets may only be
// combined when their ids mean the same thing (rows of ONE table); the type
// cannot know that — the caller does (adapter/pathset.h compares table
// pointers before touching bitmaps).
//
// basic_id_set<Id, Word> is generic over the id's integer type (what the
// members vector holds: 16-bit ids for a registry of a few dozen entries,
// 32-bit for table rows, 64-bit when a table hands those out) and the bitmap
// word (the machine's widest single-instruction integer — 64 bits everywhere
// the build runs, 32 where it is not). `id_set` is the toolkit's default
// instantiation, the one the trie and the interners hand ids to. The laws are
// proven over three instantiations in tests/static/mapping_proofs.cpp.
//
// A worked example, used in the comments below:
//
//     id_set s;                 // {}
//     s.note(5);  s.note(70);   // {5, 70}         members [5, 70]
//     id_set o;
//     o.note(70); o.note(200);  // {70, 200}       members [70, 200]
//
//     s.has(70)             -> true       s.has(6)            -> false
//     s.united(o)           -> [5, 70, 200]   (mine, then theirs; 70 once)
//     s.intersected(o)      -> [70]
//     s.subtracted(o)       -> [5]
//     o.subtracted(s)       -> [200]
//     s.count_united(o)     -> 3          (no set built)
//     s.count_intersected(o)-> 1
//     s.where(id > 10)      -> [70]
//
// Bitmap layout for the default Word (64 bits): id 70 is bit 6 of word 1
// (70 / 64 == 1, 70 % 64 == 6); id 200 is bit 8 of word 3. `s` therefore holds
// two words and `o` four — every operation below treats a word one side does
// not have as all zeros.

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
    /// Ids per bitmap word: 64 for the default Word, 32 for std::uint32_t.
    static constexpr std::size_t word_bits = std::numeric_limits<Word>::digits;

    /// The empty set: no members, no bitmap words.
    constexpr basic_id_set() = default;

    // ── building ──────────────────────────────────────────────────────────

    /// Adds `id` to the set. Returns true when it was new, false when it was
    /// already a member (nothing changes then, and it keeps its original
    /// position in `members()`).
    ///
    ///     id_set s;
    ///     s.note(5);    // true  -> members [5]
    ///     s.note(70);   // true  -> members [5, 70]; the bitmap grows to 2 words
    ///     s.note(5);    // false -> members still [5, 70]
    ///
    /// Cost: one bit test and set; the bitmap is grown to cover `id` when
    /// it is the highest id so far (the members vector grows like a vector).
    constexpr bool note(id_type id) {
        const std::size_t w = static_cast<std::size_t>(id) / word_bits;
        if (w >= m_bits.size()) m_bits.resize(w + 1, 0);
        const Word bit = static_cast<Word>(Word{1} << (static_cast<std::size_t>(id) % word_bits));
        if (m_bits[w] & bit) return false;
        m_bits[w] |= bit;
        m_members.push_back(id);
        return true;
    }

    /// Reserves room for `members` more ids in the members vector (the
    /// bitmap sizes itself from the ids noted). Use it when the count is
    /// known, e.g. before noting every row of a filter's result.
    constexpr void reserve(std::size_t members) { m_members.reserve(members); }

    // ── membership and iteration ──────────────────────────────────────────

    /// Whether `id` is a member: one load and one bit test. An id beyond the
    /// bitmap's width is simply not a member — no growth, no exception.
    ///
    ///     s.has(70)       // true
    ///     s.has(6)        // false
    ///     s.has(1'000'000)// false (past the last word)
    [[nodiscard]] constexpr bool has(id_type id) const noexcept {
        const std::size_t w = static_cast<std::size_t>(id) / word_bits;
        return w < m_bits.size() && ((m_bits[w] >> (static_cast<std::size_t>(id) % word_bits)) & Word{1});
    }
    /// The number of members.                                    s.size() -> 2
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_members.size(); }
    /// Whether there are none.                                    id_set{}.empty() -> true
    [[nodiscard]] constexpr bool empty() const noexcept { return m_members.empty(); }
    /// The members in the order they were noted — the iteration order and
    /// the order a Python set built over this one presents.
    ///
    ///     for (auto id : s.members()) ...   // 5, then 70
    [[nodiscard]] constexpr const std::vector<id_type>& members() const noexcept { return m_members; }
    /// The i-th member in insertion order (no bounds check).     s[1] -> 70
    [[nodiscard]] constexpr id_type operator[](std::size_t i) const noexcept { return m_members[i]; }

    // ── deriving sets ─────────────────────────────────────────────────────

    /// An EMPTY set over the same id space, with its bitmap already sized to
    /// this one's width — the starting point for a set that will hold a
    /// subset of these ids, so noting them never regrows the bitmap.
    ///
    ///     id_set out = s.sibling();   // {}, bitmap 2 words wide like s
    [[nodiscard]] constexpr basic_id_set sibling() const {
        basic_id_set out;
        out.m_bits.resize(m_bits.size(), 0);
        return out;
    }
    /// The members for which pred(id) is true, in the same order.
    ///
    ///     s.where([](auto id) { return id > 10; })   // [70]
    ///
    /// This is how a PathSet filter works: the predicate reads the table
    /// (suffix of the row's name, is-absolute of the row) and the result is a
    /// set over the same table.
    template <class Pred>
    [[nodiscard]] constexpr basic_id_set where(Pred pred) const {
        basic_id_set out = sibling();
        for (const id_type id : m_members) {
            if (pred(id)) out.note(id);
        }
        return out;
    }
    /// this ∪ o, ordered "mine, then theirs": every member of this set in its
    /// order, then every member of `o` not already present, in `o`'s order.
    ///
    ///     s.united(o)   // [5, 70, 200]
    ///     o.united(s)   // [70, 200, 5]
    ///
    /// Cost: one `note` per member of either set — each a bit test.
    [[nodiscard]] constexpr basic_id_set united(const basic_id_set& o) const {
        basic_id_set out = m_bits.size() >= o.m_bits.size() ? sibling() : o.sibling();
        for (const id_type id : m_members) out.note(id);
        for (const id_type id : o.m_members) out.note(id);
        return out;
    }
    /// this ∩ o, in this set's order: my members that `o` also has.
    ///
    ///     s.intersected(o)   // [70]
    ///
    /// Cost: one bit test in `o` per member of this set.
    [[nodiscard]] constexpr basic_id_set intersected(const basic_id_set& o) const {
        return where([&](id_type id) { return o.has(id); });
    }
    /// this ∖ o, in this set's order: my members that `o` does not have.
    ///
    ///     s.subtracted(o)   // [5]
    ///     o.subtracted(s)   // [200]
    [[nodiscard]] constexpr basic_id_set subtracted(const basic_id_set& o) const {
        return where([&](id_type id) { return !o.has(id); });
    }

    // ── counting without building ─────────────────────────────────────────
    // When only the SIZE of an algebra result is wanted, the members are
    // never touched: the two bitmaps are combined word by word and each
    // word's bits are counted (std::popcount — one instruction where the
    // build may emit POPCNT, see the toolkit note). A word that only one
    // side has is all zeros on the other. Over 1M paths: ~25 us against
    // 570-2170 us for the built set.

    /// |this ∪ o|.                                   s.count_united(o) -> 3
    ///
    /// Words both sides have: popcount(mine | theirs); then the tail of
    /// whichever bitmap is longer, counted on its own.
    [[nodiscard]] constexpr std::size_t count_united(const basic_id_set& o) const noexcept {
        const std::size_t common = m_bits.size() < o.m_bits.size() ? m_bits.size() : o.m_bits.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < common; ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i] | o.m_bits[i]));
        for (std::size_t i = common; i < m_bits.size(); ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i]));
        for (std::size_t i = common; i < o.m_bits.size(); ++i) n += static_cast<std::size_t>(std::popcount(o.m_bits[i]));
        return n;
    }
    /// |this ∩ o|.                                   s.count_intersected(o) -> 1
    ///
    /// Only the words both sides have can contribute: popcount(mine & theirs).
    [[nodiscard]] constexpr std::size_t count_intersected(const basic_id_set& o) const noexcept {
        const std::size_t common = m_bits.size() < o.m_bits.size() ? m_bits.size() : o.m_bits.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < common; ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i] & o.m_bits[i]));
        return n;
    }
    /// |this ∖ o|.                                   s.count_subtracted(o) -> 1
    ///                                               o.count_subtracted(s) -> 1
    /// Common words: popcount(mine & ~theirs); my tail beyond `o`'s width
    /// counts whole (nothing there to subtract).
    [[nodiscard]] constexpr std::size_t count_subtracted(const basic_id_set& o) const noexcept {
        const std::size_t common = m_bits.size() < o.m_bits.size() ? m_bits.size() : o.m_bits.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < common; ++i) n += static_cast<std::size_t>(std::popcount(static_cast<Word>(m_bits[i] & ~o.m_bits[i])));
        for (std::size_t i = common; i < m_bits.size(); ++i) n += static_cast<std::size_t>(std::popcount(m_bits[i]));
        return n;
    }

    // ── accounting ────────────────────────────────────────────────────────

    /// The bytes this set holds: the members vector's capacity plus the
    /// bitmap's — exact, from the containers, so a component can report its
    /// own size (PathSet.stats()["member_bytes"]) rather than guess from RSS.
    ///
    ///     s.bytes()   // 2 ids * 4 + 2 words * 8 = 24 (with exact-fit capacities)
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_members.capacity() * sizeof(id_type) + m_bits.capacity() * sizeof(Word);
    }

private:
    std::vector<id_type> m_members;   // insertion order
    std::vector<Word> m_bits;         // membership per id, word_bits ids per word
};

// The default: 32-bit ids (trie rows, interner ids) in 64-bit words.
using id_set = basic_id_set<>;

}  // namespace pygim::mapping
