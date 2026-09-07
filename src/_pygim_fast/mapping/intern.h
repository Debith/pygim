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
//
// A worked example, used in the comments below (either engine gives the
// same ids — the engine only decides how a lookup finds them):
//
//     hashed_interner t;        // arena "", offsets [0]           size 0
//     t.intern("home")  -> 0    // arena "home", offsets [0, 4]    size 1
//     t.intern("var")   -> 1    // arena "homevar", offsets [0, 4, 7]
//     t.intern("home")  -> 0    // already there: nothing appended
//     t.find("usr")     -> npos // find never adds
//     t[1]              -> "var"   (arena bytes 4..7, a view, no copy)
//     t.intern("")      -> 2    // the empty string is a key; offsets [0, 4, 7, 7]
//     t.intern("hom")   -> 3    // a prefix is a distinct key
//     t.size()          -> 4
//
// In the flat engine the same four ids sit in a sorted index by their text,
// [2 (""), 3 ("hom"), 0 ("home"), 1 ("var")]; in the hashed engine each id
// sits in the slot its text hashes to (plus one, so 0 can mean empty).

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

/// What every interner engine provides. `I::id_type` is the id's integer
/// type, `I::npos` the "absent" id, `I::hashed` says which engine it is
/// (a fact of the contract, like `storage::ordered`).
///
///     intern("home")   find-or-add: the id, new ids in insertion order
///     find("usr")      the id, or npos — never adds
///     operator[](id)   the bytes of an id, as a view into the arena
///     size()           how many distinct strings
///     bytes()          what the table holds, exactly
///     reserve(n, b)    room for n more ids and b more bytes of text
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

// ── the arena both engines share ────────────────────────────────────────────
/// Every distinct string once, contiguous, with one offset per string plus a
/// closing one: id -> bytes is two loads (offsets[id], offsets[id + 1]) and a
/// view into the text, never a copy. The engines add the index that finds an
/// id from its bytes.
class intern_arena {
public:
    using id_type = std::uint32_t;
    /// The "absent" id (what find() answers for a string not present).
    static constexpr id_type npos = std::numeric_limits<id_type>::max();

    /// The empty arena: no text, the single closing offset 0.
    constexpr intern_arena() : m_offsets{0} {}

    /// The bytes of `id`, as a view into the arena (valid for the arena's
    /// lifetime; the arena only ever appends, so earlier views stay valid
    /// until the text buffer reallocates — hold ids, not views, across
    /// inserts).                                        t[1] -> "var", t[2] -> ""
    [[nodiscard]] constexpr std::string_view operator[](id_type id) const noexcept {
        return {m_text.data() + m_offsets[id], static_cast<std::size_t>(m_offsets[id + 1] - m_offsets[id])};
    }
    /// How many distinct strings.                        t.size() -> 4
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_offsets.size() - 1; }
    /// The bytes the arena holds: text capacity plus the offsets.
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_text.capacity() + m_offsets.capacity() * sizeof(std::uint64_t);
    }
    /// Room for `ids` more strings and `text_bytes` more bytes of text
    /// without reallocating (0 text bytes leaves the text buffer alone).
    constexpr void reserve(std::size_t ids, std::size_t text_bytes) {
        m_offsets.reserve(ids + 1);
        if (text_bytes) m_text.reserve(text_bytes);
    }

protected:
    /// Appends `s` and returns its id: the next index. Only an engine calls
    /// this, after its index has said the string is new.
    ///
    ///     append("var") with arena "home" -> id 1, arena "homevar", offsets [0, 4, 7]
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
/// The ids kept sorted by their text; a lookup is a binary search comparing
/// the query against the arena's bytes. No hashing, no empty slots: the
/// smallest table there is, and the natural engine for constant evaluation
/// and for tables of a few dozen names. Inserting a new string shifts the
/// ids after it in the index (O(n)), which is why it is not the engine for a
/// table that grows to millions.
class flat_interner : public intern_arena {
public:
    static constexpr bool hashed = false;

    constexpr flat_interner() = default;

    /// The id of `s`, adding it when new.
    ///
    ///     t.intern("home") -> 0   index [0]
    ///     t.intern("var")  -> 1   index [0, 1]         ("home" < "var")
    ///     t.intern("")     -> 2   index [2, 0, 1]      ("" sorts first)
    ///     t.intern("hom")  -> 3   index [2, 3, 0, 1]   ("hom" < "home")
    ///     t.intern("home") -> 0   found at index[2]: nothing appended
    ///
    /// Cost: a binary search (log n string compares) plus, for a new
    /// string, an insert into the index vector.
    [[nodiscard]] constexpr id_type intern(std::string_view s) {
        const auto pos = lower_bound(s);
        if (pos != m_sorted.end() && (*this)[*pos] == s) return *pos;
        const id_type id = append(s);
        m_sorted.insert(m_sorted.begin() + (pos - m_sorted.begin()), id);
        return id;
    }
    /// The id of `s`, or npos. Never adds.                t.find("hom") -> 3, t.find("usr") -> npos
    [[nodiscard]] constexpr id_type find(std::string_view s) const noexcept {
        const auto pos = lower_bound(s);
        return pos != m_sorted.end() && (*this)[*pos] == s ? *pos : npos;
    }
    /// The arena's bytes plus the sorted index.
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return intern_arena::bytes() + m_sorted.capacity() * sizeof(id_type);
    }
    /// Room for `ids` more strings in the arena and the index.
    constexpr void reserve(std::size_t ids, std::size_t text_bytes) {
        intern_arena::reserve(ids, text_bytes);
        m_sorted.reserve(ids);
    }

private:
    using index_iter = std::vector<id_type>::iterator;
    using index_citer = std::vector<id_type>::const_iterator;
    /// The first index position whose id's text is not less than `s`: where
    /// `s` is, or where it would be inserted.
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
/// A power-of-two slot table over the arena: a slot holds `id + 1` (so 0 is
/// "empty"), a lookup hashes the bytes (FNV-1a, spread by mix64), lands on a
/// slot and probes linearly until it meets the string or an empty slot. The
/// table doubles at load factor 1/2, so probes stay short (about 1.5 on
/// average for a hit). O(1) expected, and still constexpr: proofs may build
/// one at compile time.
class hashed_interner : public intern_arena {
public:
    static constexpr bool hashed = true;

    /// 64 empty slots (the minimum; grows on demand).
    constexpr hashed_interner() : m_slots(64, 0) {}

    /// The id of `s`, adding it when new.
    ///
    ///     t.intern("home") -> 0   the slot "home" hashes to now holds 1 (id 0 + 1)
    ///     t.intern("var")  -> 1   its slot holds 2
    ///     t.intern("home") -> 0   the probe meets the same bytes: nothing appended
    ///
    /// Cost: one hash of the bytes, one probe sequence, and on a hit one
    /// string compare. When an insert brings the table past half full it
    /// doubles and every id is re-placed (ids themselves never change —
    /// see the growth law in the proofs).
    [[nodiscard]] constexpr id_type intern(std::string_view s) {
        const std::size_t i = probe(s);
        if (m_slots[i]) return m_slots[i] - 1;
        const id_type id = append(s);
        m_slots[i] = id + 1;
        if (size() * 2 > m_slots.size()) grow(m_slots.size() * 2);
        return id;
    }
    /// The id of `s`, or npos. Never adds — a miss stops at the first empty
    /// slot on the probe path.                              t.find("usr") -> npos
    [[nodiscard]] constexpr id_type find(std::string_view s) const noexcept {
        const id_type slot = m_slots[probe(s)];
        return slot ? slot - 1 : npos;
    }
    /// The arena's bytes plus the slot table (at least twice the ids, times 4).
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return intern_arena::bytes() + m_slots.capacity() * sizeof(id_type);
    }
    /// Room for `ids` more strings: the slot table is resized ONCE to the
    /// power of two that keeps `ids` at load <= 1/2 (hash::slots_for), so a
    /// bulk insert never rehashes on the way.
    ///
    ///     t.reserve(1000, 4096)   -> 2048 slots; the existing ids keep their numbers
    constexpr void reserve(std::size_t ids, std::size_t text_bytes) {
        intern_arena::reserve(ids, text_bytes);
        if (ids * 2 > m_slots.size()) grow(hash::slots_for(ids));
    }

private:
    /// The slot where `s` is, or the empty slot where it would go: hash,
    /// mask to the table, then step by one until the bytes match or a slot
    /// is empty.
    [[nodiscard]] constexpr std::size_t probe(std::string_view s) const noexcept {
        const std::size_t mask = m_slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(hash::mix64(hash::fnv1a(s))) & mask;
        while (m_slots[i] && (*this)[m_slots[i] - 1] != s) i = (i + 1) & mask;
        return i;
    }
    /// Rebuilds the slot table at `new_size` (a power of two): every id is
    /// re-placed from its bytes. Ids and the arena are untouched.
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
