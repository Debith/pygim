#pragma once
// pathlike/path_table.h — many paths as ONE table (prototype of the PathSet storage).
//
// A PathSet does not hold N path objects. It holds
//   * a segment_table: every distinct path component ("home", "d17",
//     "file9.yaml") stored ONCE — dictionary encoding, the flyweight CPython
//     applies to identifiers;
//   * a path_table: one row per distinct path, (parent row, segment id) — a
//     hash-consed trie, so the directory tree is stored once and a file path
//     costs eight bytes plus its share of the unique names;
//   * anchors as root rows: the non-segment part of a uri (absolute flag,
//     authority) is an interned key whose row has no parent.
// A path is an index. Views (adapter/pathset.h) are (table, index) and copy
// nothing; the table is append-only, so an index never dangles.
//
// Semantics come from core.h, not from string tricks: a strategy's tokenise()
// feeds exactly the anchor and segments parse_into() produces (the direct POSIX
// tokeniser is proven equal to parse_into in tests/static/pathlike_core_proofs.cpp),
// value(row) is the uri file(text) would hold, and hash(row) equals
// basic_file::hash_value() of that value — so a view and a file compare and
// hash alike.
//
// Storage: two open-addressing tables (linear probing, load factor <= 1/2).
// They are hand-rolled here because the mapping toolkit's `storage` concept
// keys by an owned K and has no heterogeneous lookup, while an intern table
// must look up by bytes that live in its own arena; promoting this shape into
// the toolkit is the follow-up. The columns (text buffer + int64 offsets,
// int32 codes) are Arrow's large_utf8 / dictionary layouts, so they export
// without copying (arrow_c_abi.h).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "core.h"

namespace pygim::pathlike {

namespace detail {
// splitmix64 finaliser: spreads a 64-bit key over the slot index bits.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}
}  // namespace detail

// Interned byte strings: each distinct string stored once, id = insertion order.
class segment_table {
public:
    static constexpr std::uint32_t npos = std::numeric_limits<std::uint32_t>::max();

    segment_table() : m_offsets{0}, m_slots(64, 0) {}

    // The id of `s`, adding it when new.
    [[nodiscard]] std::uint32_t intern(std::string_view s) {
        const std::size_t i = probe(s);
        if (m_slots[i]) return m_slots[i] - 1;
        const auto id = static_cast<std::uint32_t>(size());
        m_text.append(s);
        m_offsets.push_back(static_cast<std::int64_t>(m_text.size()));
        m_slots[i] = id + 1;
        if (size() * 2 > m_slots.size()) grow();
        return id;
    }
    [[nodiscard]] std::uint32_t find(std::string_view s) const noexcept {
        const std::uint32_t slot = m_slots[probe(s)];
        return slot ? slot - 1 : npos;
    }
    [[nodiscard]] std::string_view operator[](std::uint32_t id) const noexcept {
        return {m_text.data() + m_offsets[id], static_cast<std::size_t>(m_offsets[id + 1] - m_offsets[id])};
    }
    [[nodiscard]] std::size_t size() const noexcept { return m_offsets.size() - 1; }
    [[nodiscard]] std::size_t bytes() const noexcept {
        return m_text.capacity() + m_offsets.capacity() * sizeof(std::int64_t) + m_slots.capacity() * sizeof(std::uint32_t);
    }
    // Arrow large_utf8 layout: the text and its int64 offsets.
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    [[nodiscard]] const std::vector<std::int64_t>& offsets() const noexcept { return m_offsets; }

private:
    [[nodiscard]] std::size_t probe(std::string_view s) const noexcept {
        const std::size_t mask = m_slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(detail::mix64(detail::fnv1a(s))) & mask;
        while (m_slots[i] && (*this)[m_slots[i] - 1] != s) i = (i + 1) & mask;
        return i;
    }
    void grow() {
        std::vector<std::uint32_t> slots(m_slots.size() * 2, 0);
        const std::size_t mask = slots.size() - 1;
        for (std::uint32_t id = 0; id < size(); ++id) {
            std::size_t i = static_cast<std::size_t>(detail::mix64(detail::fnv1a((*this)[id]))) & mask;
            while (slots[i]) i = (i + 1) & mask;
            slots[i] = id + 1;
        }
        m_slots.swap(slots);
    }

    std::string m_text;
    std::vector<std::int64_t> m_offsets;
    std::vector<std::uint32_t> m_slots;   // 0 = empty, else id + 1
};

// The path table: a hash-consed trie of (parent, segment) rows.
class path_table {
public:
    static constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
    // A segment that belongs to the anchor (a drive, a UNC share, the "//"
    // root's empty segment) carries this bit: such rows have no name and are
    // their own parent, exactly as basic_file treats anchor segments.
    static constexpr std::uint32_t anchor_bit = 0x8000'0000u;

    struct row {
        std::uint32_t parent;   // `none` for an anchor row
        std::uint32_t name;     // segment id (| anchor_bit); for an anchor row: the anchor key's id
    };

    struct anchor_info {
        bool absolute = false;
        bool has_authority = false;
        std::string_view authority;
    };

    path_table() : m_slots(64, 0) {}

    // ── insertion (find-or-add), from text, from a value, from another table ──
    template <class Strategy>
    [[nodiscard]] std::uint32_t insert(std::string_view text) {
        inserter in{*this};
        Strategy::tokenise(text, in);
        return in.cur;
    }
    template <class Strategy>
    [[nodiscard]] std::uint32_t insert(const uri& u) {
        inserter in{*this};
        feed<Strategy>(u, in);
        return in.cur;
    }
    [[nodiscard]] std::uint32_t insert_from(const path_table& other, std::uint32_t r) {
        const row& x = other.m_rows[r];
        const std::uint32_t parent = x.parent == none ? none : insert_from(other, x.parent);
        return child(parent, m_segments.intern(other.m_segments[x.name & ~anchor_bit]) | (x.name & anchor_bit));
    }

    // Maps MANY rows of `from` into a table, memoised per source row: a set of
    // N paths sharing a tree maps in O(rows) lookups, not O(N x depth).
    // With `into` the rows are added (find-or-add); without, only looked up
    // in `in` (`none` when absent).
    class row_map {
    public:
        row_map(const path_table& in, const path_table& from, path_table* into = nullptr)
            : m_in(in), m_from(from), m_into(into), m_memo(from.size(), unmapped) {}
        [[nodiscard]] std::uint32_t operator()(std::uint32_t r) {
            std::uint32_t& m = m_memo[r];
            if (m != unmapped) return m;
            const row& x = m_from.m_rows[r];
            std::uint32_t parent = none;
            if (x.parent != none) {
                parent = (*this)(x.parent);
                if (parent == none) return m = none;
            }
            const std::string_view text = m_from.m_segments[x.name & ~anchor_bit];
            if (m_into) return m = m_into->child(parent, m_into->m_segments.intern(text) | (x.name & anchor_bit));
            const std::uint32_t id = m_in.m_segments.find(text);
            return m = id == segment_table::npos ? none : m_in.find_child(parent, id | (x.name & anchor_bit));
        }

    private:
        static constexpr std::uint32_t unmapped = none - 1;
        const path_table& m_in;
        const path_table& m_from;
        path_table* m_into;
        std::vector<std::uint32_t> m_memo;
    };

    // ── lookup (`none` when absent) ───────────────────────────────────────
    template <class Strategy>
    [[nodiscard]] std::uint32_t find(std::string_view text) const {
        finder f{*this};
        Strategy::tokenise(text, f);
        return f.cur;
    }
    template <class Strategy>
    [[nodiscard]] std::uint32_t find(const uri& u) const {
        finder f{*this};
        feed<Strategy>(u, f);
        return f.cur;
    }
    [[nodiscard]] std::uint32_t find(const path_table& other, std::uint32_t r) const {
        const row& x = other.m_rows[r];
        std::uint32_t parent = none;
        if (x.parent != none) {
            parent = find(other, x.parent);
            if (parent == none) return none;
        }
        const std::uint32_t id = m_segments.find(other.m_segments[x.name & ~anchor_bit]);
        return id == segment_table::npos ? none : find_child(parent, id | (x.name & anchor_bit));
    }

    // ── rows ──────────────────────────────────────────────────────────────
    [[nodiscard]] std::size_t size() const noexcept { return m_rows.size(); }
    [[nodiscard]] bool is_anchor(std::uint32_t r) const noexcept { return m_rows[r].parent == none; }
    [[nodiscard]] bool has_name(std::uint32_t r) const noexcept { return !is_anchor(r) && !(m_rows[r].name & anchor_bit); }
    [[nodiscard]] std::string_view name(std::uint32_t r) const noexcept {
        return has_name(r) ? m_segments[m_rows[r].name] : std::string_view{};
    }
    [[nodiscard]] std::uint32_t name_id(std::uint32_t r) const noexcept { return m_rows[r].name & ~anchor_bit; }
    // pathlib's parent: the anchor (or ".") is its own parent.
    [[nodiscard]] std::uint32_t parent(std::uint32_t r) const noexcept { return has_name(r) ? m_rows[r].parent : r; }
    // Named components below the anchor (a drive or share row is part of the anchor).
    [[nodiscard]] std::uint32_t depth(std::uint32_t r) const noexcept {
        std::uint32_t d = 0;
        for (; !is_anchor(r); r = m_rows[r].parent) d += (m_rows[r].name & anchor_bit) ? 0u : 1u;
        return d;
    }
    [[nodiscard]] std::uint32_t anchor_of(std::uint32_t r) const noexcept {
        while (!is_anchor(r)) r = m_rows[r].parent;
        return r;
    }
    [[nodiscard]] anchor_info anchor(std::uint32_t r) const noexcept {
        const std::string_view key = m_segments[m_rows[anchor_of(r)].name];
        const auto flags = static_cast<unsigned char>(key[0]);
        return {(flags & 1u) != 0, (flags & 2u) != 0, key.substr(1)};
    }
    // Ancestors closest first, basic_file::parents' rule (the empty relative
    // parent "." is not listed; an anchored path ends at its anchor).
    [[nodiscard]] std::vector<std::uint32_t> parents(std::uint32_t r) const {
        std::vector<std::uint32_t> out;
        std::vector<std::uint32_t> ch;
        chain(r, ch);
        const anchor_info a = anchor(r);
        const bool anchored = a.absolute || a.has_authority || (ch.size() > 1 && (m_rows[ch[1]].name & anchor_bit));
        std::uint32_t cur = r;
        while (has_name(cur)) {
            cur = m_rows[cur].parent;
            if (!has_name(cur) && !anchored) break;
            out.push_back(cur);
        }
        return out;
    }

    // f(row) for every row from the anchor down to r, root first.
    template <class F>
    void for_chain(std::uint32_t r, F& f) const {
        if (m_rows[r].parent != none) for_chain(m_rows[r].parent, f);
        f(r);
    }

    // The value of a row: the uri file(text) holds for the same path.
    [[nodiscard]] uri value(std::uint32_t r) const {
        const anchor_info a = anchor(r);
        uri u;
        u.scheme = "file";
        u.absolute = a.absolute;
        u.has_authority = a.has_authority;
        u.authority = std::string(a.authority);
        auto collect = [&](std::uint32_t row) {
            if (!is_anchor(row)) u.segments.emplace_back(m_segments[m_rows[row].name & ~anchor_bit]);
        };
        for_chain(r, collect);
        return u;
    }

    // == basic_file::hash_value() of value(r), without building the value.
    [[nodiscard]] std::uint64_t hash(std::uint32_t r) const noexcept {
        const anchor_info a = anchor(r);
        std::uint64_t h = detail::fnv_basis;
        if (a.has_authority) h = detail::mix_string(h, a.authority);
        h ^= (a.has_authority ? 2u : 0u) | (a.absolute ? 1u : 0u);
        h *= detail::fnv_prime;
        auto mix = [&](std::uint32_t row) {
            if (!is_anchor(row)) h = detail::mix_string(h, m_segments[m_rows[row].name & ~anchor_bit]);
        };
        for_chain(r, mix);
        return h;
    }

    // The anchor as a uri holding only the anchor-part segments: what a
    // strategy's anchor()/is_absolute()/is_anchored() look at (nothing further).
    [[nodiscard]] uri head_of(std::uint32_t r) const {
        const anchor_info a = anchor(r);
        uri head;
        head.absolute = a.absolute;
        head.has_authority = a.has_authority;
        head.authority = std::string(a.authority);
        auto put = [&](std::uint32_t row) {
            if (!is_anchor(row) && (m_rows[row].name & anchor_bit)) head.segments.emplace_back(m_segments[m_rows[row].name & ~anchor_bit]);
        };
        for_chain(r, put);
        return head;
    }
    // pathlib's is_absolute(): rooted on POSIX; rooted AND on a drive or share on Windows.
    template <class Strategy>
    [[nodiscard]] bool is_absolute(std::uint32_t r) const { return Strategy::is_absolute(head_of(r)); }

    // pathlib's str() of a row — Strategy::render()'s rule (the anchor, then
    // the components joined; "." when empty) without building the value: the
    // anchor is rendered from a uri holding only the anchor-part segments.
    template <class Strategy>
    [[nodiscard]] std::string render(std::uint32_t r) const {
        const anchor_info a = anchor(r);
        uri head;
        head.absolute = a.absolute;
        head.has_authority = a.has_authority;
        head.authority = std::string(a.authority);
        std::string tail;
        auto put = [&](std::uint32_t row) {
            if (is_anchor(row)) return;
            const std::uint32_t f = m_rows[row].name;
            if (f & anchor_bit) {
                head.segments.emplace_back(m_segments[f & ~anchor_bit]);
                return;
            }
            if (!tail.empty()) tail += Strategy::sep;
            tail += m_segments[f];
        };
        for_chain(r, put);
        std::string out = Strategy::anchor(head);
        out += tail;
        return out.empty() ? std::string(".") : out;
    }

    [[nodiscard]] const segment_table& segments() const noexcept { return m_segments; }
    [[nodiscard]] const std::vector<row>& rows() const noexcept { return m_rows; }
    [[nodiscard]] std::size_t bytes() const noexcept {
        return m_segments.bytes() + m_rows.capacity() * sizeof(row) + m_slots.capacity() * sizeof(std::uint32_t);
    }

private:
    // The tokenise() sinks: one descends adding rows, the other only looks.
    struct inserter {
        path_table& t;
        std::uint32_t cur = none;
        std::size_t anchor_segs = 0;
        std::size_t depth = 0;
        void anchor(bool absolute, bool has_authority, std::string_view authority, std::size_t anchor_segments) {
            anchor_segs = anchor_segments;
            cur = t.anchor_row(absolute, has_authority, authority);
        }
        void segment(std::string_view s) {
            std::uint32_t field = t.m_segments.intern(s);
            if (depth++ < anchor_segs) field |= anchor_bit;
            cur = t.child(cur, field);
        }
    };
    struct finder {
        const path_table& t;
        std::uint32_t cur = none;
        std::size_t anchor_segs = 0;
        std::size_t depth = 0;
        bool started = false;
        void anchor(bool absolute, bool has_authority, std::string_view authority, std::size_t anchor_segments) {
            anchor_segs = anchor_segments;
            started = true;
            cur = t.find_anchor_row(absolute, has_authority, authority);
        }
        void segment(std::string_view s) {
            const bool in_anchor = depth++ < anchor_segs;
            if (cur == none) return;
            const std::uint32_t id = t.m_segments.find(s);
            cur = id == segment_table::npos ? none : t.find_child(cur, id | (in_anchor ? anchor_bit : 0u));
        }
    };

    // A parsed value through the sink interface (what a strategy's generic tokenise() does).
    template <class Strategy, class Sink>
    static void feed(const uri& u, Sink& sink) {
        sink.anchor(u.absolute, u.has_authority, u.authority, Strategy::anchor_segments(u));
        for (const std::string& s : u.segments) sink.segment(s);
    }

    // The anchor key: one flags byte (bit 0 absolute, bit 1 authority) then the authority.
    static constexpr char anchor_flags(bool absolute, bool has_authority) noexcept {
        return static_cast<char>(0x40 | (absolute ? 1 : 0) | (has_authority ? 2 : 0));
    }
    [[nodiscard]] std::uint32_t anchor_row(bool absolute, bool has_authority, std::string_view authority) {
        const char flags = anchor_flags(absolute, has_authority);
        if (authority.empty()) return child(none, m_segments.intern(std::string_view(&flags, 1)));
        std::string key(1, flags);
        key += authority;
        return child(none, m_segments.intern(key));
    }
    [[nodiscard]] std::uint32_t find_anchor_row(bool absolute, bool has_authority, std::string_view authority) const {
        const char flags = anchor_flags(absolute, has_authority);
        std::uint32_t id = segment_table::npos;
        if (authority.empty()) {
            id = m_segments.find(std::string_view(&flags, 1));
        } else {
            std::string key(1, flags);
            key += authority;
            id = m_segments.find(key);
        }
        return id == segment_table::npos ? none : find_child(none, id);
    }

    // Root-first chain of rows from the anchor row down to r (inclusive).
    void chain(std::uint32_t r, std::vector<std::uint32_t>& out) const {
        out.clear();
        for (std::uint32_t c = r; c != none; c = m_rows[c].parent) out.push_back(c);
        std::reverse(out.begin(), out.end());
    }

    [[nodiscard]] static std::uint64_t key_hash(std::uint32_t parent, std::uint32_t name) noexcept {
        return detail::mix64((static_cast<std::uint64_t>(parent) << 32) | name);
    }
    [[nodiscard]] std::size_t probe(std::uint32_t parent, std::uint32_t name) const noexcept {
        const std::size_t mask = m_slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(key_hash(parent, name)) & mask;
        while (m_slots[i]) {
            const row& x = m_rows[m_slots[i] - 1];
            if (x.parent == parent && x.name == name) break;
            i = (i + 1) & mask;
        }
        return i;
    }
    [[nodiscard]] std::uint32_t find_child(std::uint32_t parent, std::uint32_t name) const noexcept {
        if (name == segment_table::npos) return none;
        const std::uint32_t slot = m_slots[probe(parent, name)];
        return slot ? slot - 1 : none;
    }
    [[nodiscard]] std::uint32_t child(std::uint32_t parent, std::uint32_t name) {
        const std::size_t i = probe(parent, name);
        if (m_slots[i]) return m_slots[i] - 1;
        const auto id = static_cast<std::uint32_t>(m_rows.size());
        m_rows.push_back({parent, name});
        m_slots[i] = id + 1;
        if (m_rows.size() * 2 > m_slots.size()) grow();
        return id;
    }
    void grow() {
        std::vector<std::uint32_t> slots(m_slots.size() * 2, 0);
        const std::size_t mask = slots.size() - 1;
        for (std::uint32_t id = 0; id < m_rows.size(); ++id) {
            std::size_t i = static_cast<std::size_t>(key_hash(m_rows[id].parent, m_rows[id].name)) & mask;
            while (slots[i]) i = (i + 1) & mask;
            slots[i] = id + 1;
        }
        m_slots.swap(slots);
    }

    segment_table m_segments;
    std::vector<row> m_rows;
    std::vector<std::uint32_t> m_slots;   // 0 = empty, else row + 1
};

}  // namespace pygim::pathlike
