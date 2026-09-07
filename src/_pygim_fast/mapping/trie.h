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
    static constexpr row_id none = std::numeric_limits<row_id>::max();
    static constexpr key_type no_key = std::numeric_limits<key_type>::max();

    struct row {
        row_id parent;   // `none` for a root
        key_type key;
    };

    constexpr trie() : m_slots(64, 0) {}

    // ── find-or-add / lookup ───────────────────────────────────────────────
    [[nodiscard]] constexpr row_id child(row_id parent, key_type key) {
        const std::size_t i = probe(parent, key);
        if (m_slots[i]) return m_slots[i] - 1;
        const auto id = static_cast<row_id>(m_rows.size());
        m_rows.push_back({parent, key});
        m_slots[i] = id + 1;
        if (m_rows.size() * 2 > m_slots.size()) grow(m_slots.size() * 2);
        return id;
    }
    [[nodiscard]] constexpr row_id find_child(row_id parent, key_type key) const noexcept {
        if (key == no_key) return none;
        const row_id slot = m_slots[probe(parent, key)];
        return slot ? slot - 1 : none;
    }
    [[nodiscard]] constexpr row_id root(key_type key) { return child(none, key); }
    [[nodiscard]] constexpr row_id find_root(key_type key) const noexcept { return find_child(none, key); }

    // ── rows ───────────────────────────────────────────────────────────────
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_rows.size(); }
    [[nodiscard]] constexpr const std::vector<row>& rows() const noexcept { return m_rows; }
    [[nodiscard]] constexpr bool is_root(row_id r) const noexcept { return m_rows[r].parent == none; }
    [[nodiscard]] constexpr row_id parent(row_id r) const noexcept { return m_rows[r].parent; }
    [[nodiscard]] constexpr key_type key(row_id r) const noexcept { return m_rows[r].key; }
    // Rows above r, root excluded: the root's depth is 0.
    [[nodiscard]] constexpr std::uint32_t depth(row_id r) const noexcept {
        std::uint32_t d = 0;
        for (; !is_root(r); r = m_rows[r].parent) ++d;
        return d;
    }
    [[nodiscard]] constexpr row_id root_of(row_id r) const noexcept {
        while (!is_root(r)) r = m_rows[r].parent;
        return r;
    }

    // f(rows, n): the chain of rows LEAF FIRST — rows[0] is r, rows[n - 1]
    // the root — gathered once into a stack buffer as the walk goes up.
    // Callers iterate it backwards; one walk serves whatever they compute.
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
    // f(row) for every row from the root down to r.
    template <class F>
    constexpr void for_chain(row_id r, F& f) const {
        with_chain(r, [&](const row_id* rows, std::size_t n) {
            for (std::size_t i = n; i-- > 0;) f(rows[i]);
        });
    }

    // ── capacity ───────────────────────────────────────────────────────────
    constexpr void reserve(std::size_t rows) {
        m_rows.reserve(rows);
        if (rows * 2 > m_slots.size()) grow(hash::slots_for(rows));
    }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        return m_rows.capacity() * sizeof(row) + m_slots.capacity() * sizeof(row_id);
    }

    // ── mapping rows of another trie into this one ─────────────────────────
    // Memoised per source row, so N chains sharing a tree map in O(rows)
    // lookups, not O(N x depth). `map_key(key)` translates a source key into
    // this trie's key space (`no_key` when it has none). With `into` the rows
    // are added (find-or-add); without, they are only looked up in `in`.
    template <class KeyMap>
    class row_map {
    public:
        constexpr row_map(const trie& in, const trie& from, trie* into, KeyMap map_key)
            : m_in(in), m_from(from), m_into(into), m_map_key(map_key), m_memo(from.size(), unmapped) {}
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

    [[nodiscard]] static constexpr std::uint64_t key_hash(row_id parent, key_type key) noexcept {
        return hash::mix64((static_cast<std::uint64_t>(parent) << 32) | key);
    }
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
