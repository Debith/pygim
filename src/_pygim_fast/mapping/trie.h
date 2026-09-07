#pragma once
// mapping/trie.h — a hash-consed trie: rows of (parent, key), each distinct
// chain stored once.
//
// CORE layer: pybind-free, constexpr throughout. A row is a 32-bit id into
// two flat columns; a key is an opaque 32-bit value (an interner id, with
// whatever flag bits the caller packs in). child(parent, key) is find-or-add,
// so two chains that share a prefix share the rows of that prefix — the
// directory tree of a path table (pathlike/path_table.h) is one instance;
// dotted config keys, module names, URL segments are the same shape. Rows are
// append-only: an id never dangles, which is what lets sets (id_set.h) and
// views hold them.
//
// Storage: two flat vectors (rows, and an open-addressing index over them —
// linear probing, load <= 1/2). No per-row heap object, ever.
//
// A worked example, used in the comments below. Keys are plain numbers here;
// in a path table they are interned segment ids.
//
//     trie t;                          rows: (none = no parent)
//     root = t.root(7)        -> 0     0: {parent none, key 7}
//     a    = t.child(root, 1) -> 1     1: {0, 1}
//     b    = t.child(a, 2)    -> 2     2: {1, 2}
//     t.child(root, 1)        -> 1     hash-consed: the same (parent, key) is the same row
//     c    = t.child(a, 3)    -> 3     3: {1, 3}   a sibling of b: it shares row 1
//     t.find_child(root, 9)   -> none  find never adds
//     t.parent(b) -> 1   t.key(b) -> 2   t.depth(b) -> 2   t.root_of(b) -> 0
//     t.with_chain(b, f)      -> f([2, 1, 0], 3)      leaf first
//     t.for_chain(b, g)       -> g(0); g(1); g(2)     root first
//
// Read as a tree: 7 -> 1 -> {2, 3}. Four rows for two two-segment chains,
// because the prefix 7 -> 1 is stored once.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "../utils/hash.h"

namespace pygim::mapping {

class trie {
public:
    using row_id = std::uint32_t;
    using key_type = std::uint32_t;
    /// The "no row" id: a root's parent, and what a lookup answers when absent.
    static constexpr row_id none = std::numeric_limits<row_id>::max();
    /// The "no key" value: what a key translator answers when a key has no
    /// counterpart, and a key find_child() never finds.
    static constexpr key_type no_key = std::numeric_limits<key_type>::max();

    /// One row: where it hangs and what it is called there.
    struct row {
        row_id parent;   // `none` for a root
        key_type key;
    };

    /// The empty trie: no rows, 64 empty index slots.
    constexpr trie() : m_slots(64, 0) {}

    // ── find-or-add / lookup ───────────────────────────────────────────────

    /// The row (parent, key), created when absent. This is the hash-consing
    /// step: every chain is built one child() at a time from its root, and a
    /// prefix already present is met, not duplicated.
    ///
    ///     t.child(root, 1) -> 1   (created)
    ///     t.child(root, 1) -> 1   (found: same row)
    ///     t.child(a, 3)    -> 3   (created under the shared row 1)
    ///
    /// Cost: one hash of (parent, key), one probe sequence; a new row is a
    /// push onto the rows column. Past half load the index doubles and every
    /// row is re-placed (row ids never change).
    [[nodiscard]] constexpr row_id child(row_id parent, key_type key) {
        const std::size_t i = probe(parent, key);
        if (m_slots[i]) return m_slots[i] - 1;
        const auto id = static_cast<row_id>(m_rows.size());
        m_rows.push_back({parent, key});
        m_slots[i] = id + 1;
        if (m_rows.size() * 2 > m_slots.size()) grow(m_slots.size() * 2);
        return id;
    }
    /// The row (parent, key), or `none`. Never adds; `no_key` is never found.
    ///
    ///     t.find_child(root, 1) -> 1      t.find_child(root, 9) -> none
    [[nodiscard]] constexpr row_id find_child(row_id parent, key_type key) const noexcept {
        if (key == no_key) return none;
        const row_id slot = m_slots[probe(parent, key)];
        return slot ? slot - 1 : none;
    }
    /// A root row: a child of `none`. A path table's anchors ("." and "/")
    /// are roots.                                           t.root(7) -> 0
    [[nodiscard]] constexpr row_id root(key_type key) { return child(none, key); }
    /// The root with `key`, or `none`.                       t.find_root(7) -> 0, t.find_root(8) -> none
    [[nodiscard]] constexpr row_id find_root(key_type key) const noexcept { return find_child(none, key); }

    // ── rows ───────────────────────────────────────────────────────────────

    /// How many rows.                                        t.size() -> 4
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_rows.size(); }
    /// The rows column itself (parent, key per row, in id order).
    [[nodiscard]] constexpr const std::vector<row>& rows() const noexcept { return m_rows; }
    /// Whether `r` has no parent.                            t.is_root(0) -> true, t.is_root(1) -> false
    [[nodiscard]] constexpr bool is_root(row_id r) const noexcept { return m_rows[r].parent == none; }
    /// The row above `r` (`none` for a root).                t.parent(b) -> 1, t.parent(root) -> none
    [[nodiscard]] constexpr row_id parent(row_id r) const noexcept { return m_rows[r].parent; }
    /// The key `r` hangs under its parent by.                t.key(b) -> 2
    [[nodiscard]] constexpr key_type key(row_id r) const noexcept { return m_rows[r].key; }
    /// Rows above r, root excluded: the root's depth is 0.
    ///
    ///     t.depth(root) -> 0   t.depth(a) -> 1   t.depth(b) -> 2
    ///
    /// Cost: one load per level (a walk up the chain).
    [[nodiscard]] constexpr std::uint32_t depth(row_id r) const noexcept {
        std::uint32_t d = 0;
        for (; !is_root(r); r = m_rows[r].parent) ++d;
        return d;
    }
    /// The root of r's chain.                                t.root_of(b) -> 0, t.root_of(root) -> 0
    [[nodiscard]] constexpr row_id root_of(row_id r) const noexcept {
        while (!is_root(r)) r = m_rows[r].parent;
        return r;
    }

    /// f(rows, n): the chain of rows LEAF FIRST — rows[0] is r, rows[n - 1]
    /// the root — gathered once into a stack buffer as the walk goes up.
    /// Callers iterate it backwards; one walk serves whatever they compute
    /// (a path table renders, hashes and builds values this way, so the
    /// anchor row is read from rows[n - 1] and the segments from rows[n - 2]
    /// down to rows[0] in the same pass). Returns whatever f returns.
    ///
    ///     t.with_chain(b, [](const row_id* rows, std::size_t n) { ... })
    ///         -> rows = [2, 1, 0], n = 3
    ///
    /// Cost: one load per level, no allocation up to 48 rows deep; a deeper
    /// chain spills into a vector. (A root-first order would need a reverse
    /// pass: measured 12% on hash(), so the buffer is left leaf-first.)
    template <class F>
    constexpr decltype(auto) with_chain(row_id r, F&& f) const {
        row_id local[chain_local];
        std::size_t n = 0;
        row_id c = r;
        for (; c != none && n < chain_local; c = m_rows[c].parent) local[n++] = c;
        if (c == none) [[likely]] return f(static_cast<const row_id*>(local), n);
        std::vector<row_id> deep(local, local + n);
        for (; c != none; c = m_rows[c].parent) deep.push_back(c);
        return f(static_cast<const row_id*>(deep.data()), deep.size());
    }
    /// f(row) for every row from the root down to r.
    ///
    ///     t.for_chain(b, g)   -> g(0), g(1), g(2)
    template <class F>
    constexpr void for_chain(row_id r, F& f) const {
        with_chain(r, [&](const row_id* rows, std::size_t n) {
            for (std::size_t i = n; i-- > 0;) f(rows[i]);
        });
    }

    // ── capacity ───────────────────────────────────────────────────────────

    /// Room for `rows` rows without rehashing: the rows column is reserved
    /// and the index resized ONCE to the power of two that keeps `rows` at
    /// load <= 1/2. Existing rows keep their ids.
    ///
    ///     t.reserve(4096)   -> 8192 index slots
    constexpr void reserve(std::size_t rows) {
        m_rows.reserve(rows);
        if (rows * 2 > m_slots.size()) grow(hash::slots_for(rows));
    }
    /// The bytes the trie holds: 8 per row of capacity plus 4 per index slot.
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_rows.capacity() * sizeof(row) + m_slots.capacity() * sizeof(row_id);
    }

    // ── mapping rows of another trie into this one ─────────────────────────
    /// Maps rows of `from` into a trie, memoised per source row, so N chains
    /// sharing a tree map in O(rows) lookups, not O(N x depth). `map_key`
    /// translates a source key into this trie's key space (`no_key` when it
    /// has none: e.g. a segment the target interner does not know). With
    /// `into` the rows are added (find-or-add, under the mapped parent);
    /// without, they are only looked up in `in` and an absent row maps to
    /// `none`.
    ///
    /// Example: `other` is the same tree with every key + 10 (root 17, then
    /// 11 -> {12, 13}), and `shift` maps a key k to k - 10.
    ///
    ///     trie::row_map look(t, other, nullptr, shift);   // lookup only
    ///     look(other_b)  -> 2      (17 -> 11 -> 12 is 7 -> 1 -> 2: row b)
    ///     look(other_c)  -> none   if t has no 7 -> 1 -> 3 yet
    ///     look(other_b)  -> 2      memoised: no second walk
    ///
    ///     trie::row_map add(t, other, &t, shift);         // find-or-add
    ///     add(other_c)   -> a new row under row 1 with key 3
    ///
    /// This is how a PathSet unites two sets over different tables: the
    /// smaller table's rows are mapped into a copy of the larger.
    template <class KeyMap>
    class row_map {
    public:
        constexpr row_map(const trie& in, const trie& from, trie* into, KeyMap map_key)
            : m_in(in), m_from(from), m_into(into), m_map_key(map_key), m_memo(from.size(), unmapped) {}
        /// The row of `r` (a row of `from`) in the target, or `none`.
        /// Recursion maps the parent first, so a chain's prefix is mapped
        /// once and reused by every row below it.
        [[nodiscard]] constexpr row_id operator()(row_id r) {
            row_id& m = m_memo[r];
            if (m != unmapped) return m;
            const row& x = m_from.m_rows[r];
            row_id parent = none;
            if (x.parent != none) {
                parent = (*this)(x.parent);
                if (parent == none) return m = none;
            }
            const key_type key = m_map_key(x.key);
            if (key == no_key) return m = none;
            return m = m_into ? m_into->child(parent, key) : m_in.find_child(parent, key);
        }

    private:
        static constexpr row_id unmapped = none - 1;
        const trie& m_in;
        const trie& m_from;
        trie* m_into;
        KeyMap m_map_key;
        std::vector<row_id> m_memo;
    };

private:
    static constexpr std::size_t chain_local = 48;   // rows kept on the stack by with_chain()

    /// The 64-bit key of a row: parent in the high half, key in the low,
    /// spread by mix64 so the low bits used as a slot index do not just
    /// repeat the key.
    [[nodiscard]] static constexpr std::uint64_t key_hash(row_id parent, key_type key) noexcept {
        return hash::mix64((static_cast<std::uint64_t>(parent) << 32) | key);
    }
    /// The slot where (parent, key) is, or the empty slot where it would go:
    /// linear probing, comparing against the rows column (the index stores
    /// only row + 1, never the key).
    [[nodiscard]] constexpr std::size_t probe(row_id parent, key_type key) const noexcept {
        const std::size_t mask = m_slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(key_hash(parent, key)) & mask;
        while (m_slots[i]) {
            const row& x = m_rows[m_slots[i] - 1];
            if (x.parent == parent && x.key == key) break;
            i = (i + 1) & mask;
        }
        return i;
    }
    /// Rebuilds the index at `new_size` (a power of two): every row is
    /// re-placed from its (parent, key). The rows column is untouched.
    constexpr void grow(std::size_t new_size) {
        std::vector<row_id> slots(new_size, 0);
        const std::size_t mask = slots.size() - 1;
        for (row_id id = 0; id < m_rows.size(); ++id) {
            std::size_t i = static_cast<std::size_t>(key_hash(m_rows[id].parent, m_rows[id].key)) & mask;
            while (slots[i]) i = (i + 1) & mask;
            slots[i] = id + 1;
        }
        m_slots.swap(slots);
    }

    std::vector<row> m_rows;
    std::vector<row_id> m_slots;   // 0 = empty, else row + 1
};

}  // namespace pygim::mapping
