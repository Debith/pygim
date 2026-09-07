#pragma once
// pathlike/path_table.h — many paths as ONE table: the path semantics over a
// mapping::trie of (parent, segment) rows and a mapping::interner of segments.
//
// A PathSet does not hold N path objects. It holds
//   * an interner (mapping/intern.h): every distinct path component ("home",
//     "d17", "file9.yaml") stored ONCE — dictionary encoding, the flyweight
//     CPython applies to identifiers;
//   * a trie (mapping/trie.h): one row per distinct path, (parent row,
//     segment id) hash-consed, so the directory tree is stored once and a
//     file path costs eight bytes plus its share of the unique names;
//   * anchors as root rows: the non-segment part of a uri (absolute flag,
//     authority) is an interned key whose row has no parent.
// A path is an index. Views (adapter/pathset.h) are (table, index) and copy
// nothing; the table is append-only, so an index never dangles.
//
// Semantics come from core.h, not from string tricks: a strategy's tokenise()
// feeds exactly the anchor and segments parse_into() produces (proven equal in
// tests/static/pathlike_core_proofs.cpp), value(row) is the uri file(text)
// would hold, and hash(row) equals basic_file::hash_value() of that value —
// so a view and a file compare and hash alike. Those two laws are proven at
// compile time over the flat interner in the same TU.
//
// The interner is a policy (mapping::interner): hashed_interner for the
// runtime table, flat_interner for small tables and constant evaluation.
// Storage is structure-of-arrays throughout: no per-row heap object, ever
// (docs/design/pathset_storage.md, docs/design/mapping_toolkit.md).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "../mapping/intern.h"
#include "../mapping/trie.h"
#include "../utils/hash.h"
#include "core.h"

namespace pygim::pathlike {

template <mapping::interner Interner>
class basic_path_table {
public:
    using trie = mapping::trie;
    using interner_type = Interner;
    static constexpr std::uint32_t none = trie::none;
    // A segment that belongs to the anchor (a drive, a UNC share, the "//"
    // root's empty segment) carries this bit: such rows have no name and are
    // their own parent, exactly as basic_file treats anchor segments.
    static constexpr std::uint32_t anchor_bit = 0x8000'0000u;

    struct anchor_info {
        bool absolute = false;
        bool has_authority = false;
        std::string_view authority;
    };

    constexpr basic_path_table() = default;

    // Room for `rows` rows and `segments` distinct segments without rehashing.
    constexpr void reserve(std::size_t rows, std::size_t segments) {
        m_trie.reserve(rows);
        m_segments.reserve(segments, 0);
    }

    // ── insertion (find-or-add), from text, from a value, from another table ──
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t insert(std::string_view text) {
        inserter in{*this};
        Strategy::tokenise(text, in);
        return in.cur;
    }
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t insert(const uri& u) {
        inserter in{*this};
        feed<Strategy>(u, in);
        return in.cur;
    }
    // The child of row `parent` named by one plain component (never an anchor
    // part): what `parent / name` is for a name without separators.
    [[nodiscard]] constexpr std::uint32_t child_of(std::uint32_t parent, std::string_view name) {
        return m_trie.child(parent, m_segments.intern(name));
    }
    [[nodiscard]] constexpr std::uint32_t insert_from(const basic_path_table& other, std::uint32_t r) {
        const std::uint32_t parent = other.m_trie.is_root(r) ? none : insert_from(other, other.m_trie.parent(r));
        const std::uint32_t key = other.m_trie.key(r);
        return m_trie.child(parent, m_segments.intern(other.m_segments[key & ~anchor_bit]) | (key & anchor_bit));
    }

    // Maps MANY rows of `from` into a table, memoised per source row AND per
    // source segment id (each distinct name is hashed once per mapping). With
    // `into` the rows are added (find-or-add); without, only looked up in
    // `in` (`none` when absent).
    class row_map {
    public:
        row_map(const basic_path_table& in, const basic_path_table& from, basic_path_table* into = nullptr)
            : m_in(in), m_from(from), m_into(into), m_seg_memo(from.m_segments.size(), unmapped),
              m_rows(in.m_trie, from.m_trie, into ? &into->m_trie : nullptr, key_map{this}) {}
        row_map(const row_map&) = delete;
        row_map& operator=(const row_map&) = delete;
        [[nodiscard]] std::uint32_t operator()(std::uint32_t r) { return m_rows(r); }

    private:
        static constexpr std::uint32_t unmapped = none - 1;
        struct key_map {
            row_map* self;
            [[nodiscard]] std::uint32_t operator()(std::uint32_t key) const { return self->translate(key); }
        };
        [[nodiscard]] std::uint32_t translate(std::uint32_t key) {
            const std::uint32_t from_id = key & ~anchor_bit;
            std::uint32_t& s = m_seg_memo[from_id];
            if (s == unmapped) {
                const std::string_view text = m_from.m_segments[from_id];
                s = m_into ? m_into->m_segments.intern(text) : m_in.m_segments.find(text);
            }
            return s == Interner::npos ? trie::no_key : (s | (key & anchor_bit));
        }
        const basic_path_table& m_in;
        const basic_path_table& m_from;
        basic_path_table* m_into;
        std::vector<std::uint32_t> m_seg_memo;
        trie::row_map<key_map> m_rows;
    };

    // ── lookup (`none` when absent) ───────────────────────────────────────
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t find(std::string_view text) const {
        finder f{*this};
        Strategy::tokenise(text, f);
        return f.cur;
    }
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t find(const uri& u) const {
        finder f{*this};
        feed<Strategy>(u, f);
        return f.cur;
    }
    [[nodiscard]] constexpr std::uint32_t find(const basic_path_table& other, std::uint32_t r) const {
        std::uint32_t parent = none;
        if (!other.m_trie.is_root(r)) {
            parent = find(other, other.m_trie.parent(r));
            if (parent == none) return none;
        }
        const std::uint32_t key = other.m_trie.key(r);
        const std::uint32_t id = m_segments.find(other.m_segments[key & ~anchor_bit]);
        return id == Interner::npos ? none : m_trie.find_child(parent, id | (key & anchor_bit));
    }

    // ── rows ──────────────────────────────────────────────────────────────
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_trie.size(); }
    [[nodiscard]] constexpr bool is_anchor(std::uint32_t r) const noexcept { return m_trie.is_root(r); }
    [[nodiscard]] constexpr bool has_name(std::uint32_t r) const noexcept {
        return !is_anchor(r) && !(m_trie.key(r) & anchor_bit);
    }
    [[nodiscard]] constexpr std::string_view name(std::uint32_t r) const noexcept {
        return has_name(r) ? m_segments[m_trie.key(r)] : std::string_view{};
    }
    [[nodiscard]] constexpr std::uint32_t name_id(std::uint32_t r) const noexcept { return m_trie.key(r) & ~anchor_bit; }
    // pathlib's parent: the anchor (or ".") is its own parent.
    [[nodiscard]] constexpr std::uint32_t parent(std::uint32_t r) const noexcept { return has_name(r) ? m_trie.parent(r) : r; }
    // Named components below the anchor (a drive or share row is part of the anchor).
    [[nodiscard]] constexpr std::uint32_t depth(std::uint32_t r) const noexcept {
        std::uint32_t d = 0;
        for (; !is_anchor(r); r = m_trie.parent(r)) d += (m_trie.key(r) & anchor_bit) ? 0u : 1u;
        return d;
    }
    [[nodiscard]] constexpr std::uint32_t anchor_of(std::uint32_t r) const noexcept { return m_trie.root_of(r); }
    [[nodiscard]] constexpr anchor_info anchor(std::uint32_t r) const noexcept { return anchor_at(anchor_of(r)); }
    // Decodes an anchor row's key (no walk).
    [[nodiscard]] constexpr anchor_info anchor_at(std::uint32_t anchor_row) const noexcept {
        const std::string_view key = m_segments[m_trie.key(anchor_row)];
        const auto flags = static_cast<unsigned char>(key[0]);
        return {(flags & 1u) != 0, (flags & 2u) != 0, key.substr(1)};
    }
    // Ancestors closest first, basic_file::parents' rule (the empty relative
    // parent "." is not listed; an anchored path ends at its anchor).
    [[nodiscard]] constexpr std::vector<std::uint32_t> parents(std::uint32_t r) const {
        return m_trie.with_chain(r, [&](const std::uint32_t* rows, std::size_t n) {
            std::vector<std::uint32_t> out;
            const anchor_info a = anchor_at(rows[n - 1]);
            const bool anchored = a.absolute || a.has_authority || (n > 1 && (m_trie.key(rows[n - 2]) & anchor_bit));
            std::uint32_t cur = r;
            while (has_name(cur)) {
                cur = m_trie.parent(cur);
                if (!has_name(cur) && !anchored) break;
                out.push_back(cur);
            }
            return out;
        });
    }

    // f(rows, n): the chain LEAF FIRST (rows[0] is r, rows[n - 1] the anchor row).
    template <class F>
    constexpr decltype(auto) with_chain(std::uint32_t r, F&& f) const {
        return m_trie.with_chain(r, static_cast<F&&>(f));
    }
    // f(row) for every row from the anchor down to r, root first.
    template <class F>
    constexpr void for_chain(std::uint32_t r, F& f) const {
        m_trie.for_chain(r, f);
    }

    // The value of a row: the uri file(text) holds for the same path.
    [[nodiscard]] constexpr uri value(std::uint32_t r) const {
        return m_trie.with_chain(r, [&](const std::uint32_t* rows, std::size_t n) {
            const anchor_info a = anchor_at(rows[n - 1]);
            uri u;
            u.scheme = "file";
            u.absolute = a.absolute;
            u.has_authority = a.has_authority;
            u.authority = std::string(a.authority);
            u.segments.reserve(n - 1);
            for (std::size_t i = n - 1; i-- > 0;) u.segments.emplace_back(m_segments[m_trie.key(rows[i]) & ~anchor_bit]);
            return u;
        });
    }

    // == basic_file::hash_value() of value(r), without building the value.
    [[nodiscard]] constexpr std::uint64_t hash(std::uint32_t r) const noexcept {
        return m_trie.with_chain(r, [&](const std::uint32_t* rows, std::size_t n) {
            const anchor_info a = anchor_at(rows[n - 1]);
            std::uint64_t h = pygim::hash::fnv_basis;
            if (a.has_authority) h = pygim::hash::mix_string(h, a.authority);
            h ^= (a.has_authority ? 2u : 0u) | (a.absolute ? 1u : 0u);
            h *= pygim::hash::fnv_prime;
            for (std::size_t i = n - 1; i-- > 0;) h = pygim::hash::mix_string(h, m_segments[m_trie.key(rows[i]) & ~anchor_bit]);
            return h;
        });
    }

    // The anchor as a uri holding only the anchor-part segments: what a
    // strategy's anchor()/is_absolute()/is_anchored() look at (nothing further).
    [[nodiscard]] constexpr uri head_of(std::uint32_t r) const {
        return m_trie.with_chain(r, [&](const std::uint32_t* rows, std::size_t n) {
            const anchor_info a = anchor_at(rows[n - 1]);
            uri head;
            head.absolute = a.absolute;
            head.has_authority = a.has_authority;
            head.authority = std::string(a.authority);
            for (std::size_t i = n - 1; i-- > 0;) {
                const std::uint32_t f = m_trie.key(rows[i]);
                if (f & anchor_bit) head.segments.emplace_back(m_segments[f & ~anchor_bit]);
            }
            return head;
        });
    }
    // pathlib's is_absolute(): rooted on POSIX; rooted AND on a drive or share on Windows.
    template <class Strategy>
    [[nodiscard]] constexpr bool is_absolute(std::uint32_t r) const { return Strategy::is_absolute(head_of(r)); }

    // pathlib's str() of a row — Strategy::render()'s rule (the anchor, then
    // the components joined; "." when empty) without building the value.
    template <class Strategy>
    [[nodiscard]] constexpr std::string render(std::uint32_t r) const {
        return m_trie.with_chain(r, [&](const std::uint32_t* rows, std::size_t n) {
            const anchor_info a = anchor_at(rows[n - 1]);
            // The common shape — no authority, no anchor-part segments — is
            // rendered in one pass into a string sized up front; its anchor
            // text is Strategy::anchor() of a bare root, computed once at run
            // time (and directly in constant evaluation, where statics are out).
            std::size_t tail_len = 0;
            bool plain = !a.has_authority;
            for (std::size_t i = 0; i + 1 < n; ++i) {
                const std::uint32_t f = m_trie.key(rows[i]);
                if (f & anchor_bit) plain = false;
                else tail_len += m_segments[f].size() + 1;
            }
            std::string out;
            if (plain) {
                if (std::is_constant_evaluated()) out = plain_anchor<Strategy>(a.absolute);
                else out = cached_plain_anchor<Strategy>(a.absolute);
                out.reserve(out.size() + tail_len);
            } else {
                uri head;
                head.absolute = a.absolute;
                head.has_authority = a.has_authority;
                head.authority = std::string(a.authority);
                for (std::size_t i = n - 1; i-- > 0;) {
                    const std::uint32_t f = m_trie.key(rows[i]);
                    if (f & anchor_bit) head.segments.emplace_back(m_segments[f & ~anchor_bit]);
                }
                out = Strategy::anchor(head);
                out.reserve(out.size() + tail_len);
            }
            bool first = true;
            for (std::size_t i = n - 1; i-- > 0;) {
                const std::uint32_t f = m_trie.key(rows[i]);
                if (f & anchor_bit) continue;
                if (!first) out += Strategy::sep;
                first = false;
                out += m_segments[f];
            }
            if (out.empty()) out = ".";
            return out;
        });
    }

    [[nodiscard]] constexpr const Interner& segments() const noexcept { return m_segments; }
    [[nodiscard]] constexpr const trie& rows() const noexcept { return m_trie; }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return m_segments.bytes() + m_trie.bytes(); }

private:
    template <class Strategy>
    [[nodiscard]] static constexpr std::string plain_anchor(bool absolute) {
        uri u;
        u.absolute = absolute;
        return Strategy::anchor(u);
    }
    // The run-time cache of the two plain anchors (a static may not live in a
    // constexpr function under C++20, hence the separate, ordinary helper).
    template <class Strategy>
    [[nodiscard]] static const std::string& cached_plain_anchor(bool absolute) {
        static const std::string abs_text = plain_anchor<Strategy>(true);
        static const std::string rel_text = plain_anchor<Strategy>(false);
        return absolute ? abs_text : rel_text;
    }

    // The tokenise() sinks: one descends adding rows, the other only looks.
    struct inserter {
        basic_path_table& t;
        std::uint32_t cur = none;
        std::size_t anchor_segs = 0;
        std::size_t depth = 0;
        constexpr void anchor(bool absolute, bool has_authority, std::string_view authority, std::size_t anchor_segments) {
            anchor_segs = anchor_segments;
            cur = t.anchor_row(absolute, has_authority, authority);
        }
        constexpr void segment(std::string_view s) {
            std::uint32_t field = t.m_segments.intern(s);
            if (depth++ < anchor_segs) field |= anchor_bit;
            cur = t.m_trie.child(cur, field);
        }
    };
    struct finder {
        const basic_path_table& t;
        std::uint32_t cur = none;
        std::size_t anchor_segs = 0;
        std::size_t depth = 0;
        constexpr void anchor(bool absolute, bool has_authority, std::string_view authority, std::size_t anchor_segments) {
            anchor_segs = anchor_segments;
            cur = t.find_anchor_row(absolute, has_authority, authority);
        }
        constexpr void segment(std::string_view s) {
            const bool in_anchor = depth++ < anchor_segs;
            if (cur == none) return;
            const std::uint32_t id = t.m_segments.find(s);
            cur = id == Interner::npos ? none : t.m_trie.find_child(cur, id | (in_anchor ? anchor_bit : 0u));
        }
    };

    // A parsed value through the sink interface (what a strategy's generic tokenise() does).
    template <class Strategy, class Sink>
    static constexpr void feed(const uri& u, Sink& sink) {
        sink.anchor(u.absolute, u.has_authority, u.authority, Strategy::anchor_segments(u));
        for (const std::string& s : u.segments) sink.segment(s);
    }

    // The anchor key: one flags byte (bit 0 absolute, bit 1 authority) then the authority.
    [[nodiscard]] static constexpr char anchor_flags(bool absolute, bool has_authority) noexcept {
        return static_cast<char>(0x40 | (absolute ? 1 : 0) | (has_authority ? 2 : 0));
    }
    // The four authority-less anchors are nearly every path's root: their rows
    // are cached so the per-path cost is an array read, not an intern.
    [[nodiscard]] static constexpr std::size_t plain_index(bool absolute, bool has_authority) noexcept {
        return (absolute ? 1u : 0u) | (has_authority ? 2u : 0u);
    }
    [[nodiscard]] constexpr std::uint32_t anchor_row(bool absolute, bool has_authority, std::string_view authority) {
        const char flags = anchor_flags(absolute, has_authority);
        if (authority.empty()) {
            std::uint32_t& cached = m_plain_anchor[plain_index(absolute, has_authority)];
            if (cached == none) cached = m_trie.root(m_segments.intern(std::string_view(&flags, 1)));
            return cached;
        }
        std::string key(1, flags);
        key += authority;
        return m_trie.root(m_segments.intern(key));
    }
    [[nodiscard]] constexpr std::uint32_t find_anchor_row(bool absolute, bool has_authority, std::string_view authority) const {
        if (authority.empty()) return m_plain_anchor[plain_index(absolute, has_authority)];   // `none` until inserted
        const char flags = anchor_flags(absolute, has_authority);
        std::string key(1, flags);
        key += authority;
        const std::uint32_t id = m_segments.find(key);
        return id == Interner::npos ? none : m_trie.find_root(id);
    }

    trie m_trie;
    Interner m_segments;
    std::uint32_t m_plain_anchor[4] = {none, none, none, none};
};

// The runtime table.
using path_table = basic_path_table<mapping::hashed_interner>;

}  // namespace pygim::pathlike
