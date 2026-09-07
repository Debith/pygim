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
//
// A worked example (POSIX strategy), used in the comments below. Segment ids
// are the interner's, row ids the trie's; they are different numberings.
//
//     path_table t;
//     r = t.insert<posix_strategy>("a/b/c.yaml")   -> 3
//         segments: 0 "@" (the relative anchor key), 1 "a", 2 "b", 3 "c.yaml"
//         rows:     0 {none, seg 0}  the "." anchor      (a root row)
//                   1 {0, seg 1}     a
//                   2 {1, seg 2}     a/b
//                   3 {2, seg 3}     a/b/c.yaml
//     t.insert<posix_strategy>("a//b/./c.yaml/")   -> 3   the same value: nothing added
//     d = t.insert<posix_strategy>("a/b/d")        -> 4   row 4 {2, seg 4 "d"}: shares rows 0..2
//     x = t.insert<posix_strategy>("/x")           -> 6   row 5 {none, seg 5 "A"} the "/" anchor, row 6 {5, seg 6 "x"}
//
//     t.parent(3) -> 2     t.name(3) -> "c.yaml"    t.depth(3) -> 3     t.anchor_of(3) -> 0
//     t.parent(0) -> 0     t.name(0) -> ""          t.depth(0) -> 0     (the anchor is its own parent)
//     t.anchor(6)  -> {absolute true, no authority}   t.is_absolute<posix_strategy>(6) -> true
//     t.render<posix_strategy>(3) -> "a/b/c.yaml"     t.render<posix_strategy>(0) -> "."     t.render<posix_strategy>(6) -> "/x"
//     t.value(3).segments -> ["a", "b", "c.yaml"]      t.hash(3) == file("a/b/c.yaml").hash_value()
//     t.parents(3) -> [2, 1]      (a/b, a — the "." parent is not listed)
//     t.parents(6) -> [5]         ("/" is listed: an anchored path ends at its anchor)
//     t.find<posix_strategy>("a/b") -> 2     t.find<posix_strategy>("a/q") -> none
//     t.child_of(2, "e") -> 7     a/b/e, created
//
// The anchor key is one flags byte (0x40 | absolute | authority << 1, so "@"
// relative, "A" absolute, "B"/"C" with an authority) followed by the
// authority text: `anchor_at(row)` decodes it back.

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
    /// The "no row" id: what a lookup answers when the path is not in the table.
    static constexpr std::uint32_t none = trie::none;
    /// A segment that belongs to the anchor (a drive, a UNC share, the "//"
    /// root's empty segment) carries this bit in its row's key: such rows
    /// have no name and are their own parent, exactly as basic_file treats
    /// anchor segments. On POSIX only "//x" produces one.
    static constexpr std::uint32_t anchor_bit = 0x8000'0000u;

    /// What an anchor row says about its chain: rooted or not, an authority
    /// (a UNC host) or not, and the authority's text.
    struct anchor_info {
        bool absolute = false;
        bool has_authority = false;
        std::string_view authority;
    };

    /// The empty table: no rows, no segments, no anchors yet.
    constexpr basic_path_table() = default;

    /// Room for `rows` rows and `segments` distinct segments without
    /// rehashing (a fresh table's rows run ~2-3x the path count, its
    /// distinct segments about 1x — adapter/pathset.h's reserve() applies
    /// that rule). Existing ids are kept.
    constexpr void reserve(std::size_t rows, std::size_t segments) {
        m_trie.reserve(rows);
        m_segments.reserve(segments, 0);
    }

    // ── insertion (find-or-add), from text, from a value, from another table ──

    /// The row for native path text, created when absent: the strategy's
    /// tokenise() streams the anchor and each segment into the table with no
    /// intermediate value built (the POSIX tokeniser allocates nothing).
    ///
    ///     t.insert<posix_strategy>("a/b/c.yaml")    -> 3   (creates rows 0..3 on a fresh table)
    ///     t.insert<posix_strategy>("a//b/./c.yaml/") -> 3   (spellings collapse: tokenise == parse_into)
    ///
    /// Cost: per segment, one interner lookup and one trie child() — about
    /// two probes; the anchor row is a cached array read.
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t insert(std::string_view text) {
        inserter in{*this};
        Strategy::tokenise(text, in);
        return in.cur;
    }
    /// The row for a parsed value: the same walk, fed from the uri's anchor
    /// flags and segments. What the flyweight store uses for a `file`.
    ///
    ///     t.insert<posix_strategy>(file("a/b/c.yaml").value())   -> 3
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t insert(const uri& u) {
        inserter in{*this};
        feed<Strategy>(u, in);
        return in.cur;
    }
    /// The child of row `parent` named by one plain component (never an
    /// anchor part): what `parent / name` is for a name without separators.
    /// The flyweight store's `p / "e"` when p's row is known.
    ///
    ///     t.child_of(2, "e")   -> 7   (a/b/e, created)
    ///     t.child_of(2, "d")   -> 4   (found)
    [[nodiscard]] constexpr std::uint32_t child_of(std::uint32_t parent, std::string_view name) {
        return m_trie.child(parent, m_segments.intern(name));
    }
    /// The row for row `r` of ANOTHER table, created when absent: the chain
    /// is walked from the root, each segment's text interned here. One row
    /// at a time — for many rows use row_map, which memoises.
    ///
    ///     u.insert_from(t, 3)   -> u's row for a/b/c.yaml
    [[nodiscard]] constexpr std::uint32_t insert_from(const basic_path_table& other, std::uint32_t r) {
        const std::uint32_t parent = other.m_trie.is_root(r) ? none : insert_from(other, other.m_trie.parent(r));
        const std::uint32_t key = other.m_trie.key(r);
        return m_trie.child(parent, m_segments.intern(other.m_segments[key & ~anchor_bit]) | (key & anchor_bit));
    }

    /// Maps MANY rows of `from` into a table, memoised per source row AND per
    /// source segment id (each distinct name is hashed once per mapping). With
    /// `into` the rows are added (find-or-add); without, only looked up in
    /// `in` (`none` when absent). The trie's row_map does the row work; this
    /// wrapper supplies the key translation: strip the anchor bit, map the
    /// segment id through the target interner, put the bit back.
    ///
    ///     path_table u;  u.insert<posix_strategy>("a/b/d");        // u has a/b/d, not a/b/c.yaml
    ///     path_table::row_map look(u, t);                           // lookup only
    ///     look(4) -> u's row for a/b/d      look(3) -> none          look(3) -> none (memoised)
    ///     path_table::row_map add(u, t, &u);                        // find-or-add
    ///     add(3)  -> a new row in u for a/b/c.yaml, under u's a/b
    ///
    /// This is how PathSet unites or intersects two sets over different
    /// tables (adapter/pathset.h): mapped once, never rendered and re-parsed.
    class row_map {
    public:
        row_map(const basic_path_table& in, const basic_path_table& from, basic_path_table* into = nullptr)
            : m_in(in), m_from(from), m_into(into), m_seg_memo(from.m_segments.size(), unmapped),
              m_rows(in.m_trie, from.m_trie, into ? &into->m_trie : nullptr, key_map{this}) {}
        row_map(const row_map&) = delete;
        row_map& operator=(const row_map&) = delete;
        /// The target row for `r` (a row of `from`), or `none`.
        [[nodiscard]] std::uint32_t operator()(std::uint32_t r) { return m_rows(r); }

    private:
        static constexpr std::uint32_t unmapped = none - 1;
        struct key_map {
            row_map* self;
            [[nodiscard]] std::uint32_t operator()(std::uint32_t key) const { return self->translate(key); }
        };
        /// A source key (segment id | anchor bit) as a target key: the id
        /// through the target interner, memoised per source id.
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

    /// The row for native path text, or `none`. Never adds: the walk stops
    /// at the first segment the table does not know.
    ///
    ///     t.find<posix_strategy>("a/b")   -> 2      t.find<posix_strategy>("a/q") -> none
    ///     t.find<posix_strategy>("./a//b") -> 2     (spellings collapse here too)
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t find(std::string_view text) const {
        finder f{*this};
        Strategy::tokenise(text, f);
        return f.cur;
    }
    /// The row for a parsed value, or `none`. What `file in pathset` uses.
    template <class Strategy>
    [[nodiscard]] constexpr std::uint32_t find(const uri& u) const {
        finder f{*this};
        feed<Strategy>(u, f);
        return f.cur;
    }
    /// The row here for row `r` of another table, or `none`: the chain
    /// walked from the root, each segment looked up (never interned) here.
    /// What `fileview == fileview` uses across tables.
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

    /// How many rows: every distinct path AND every distinct directory above
    /// one, plus the anchors.                                t.size() -> 7 in the example (8 after child_of)
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_trie.size(); }
    /// Whether `r` is an anchor row (a trie root).           t.is_anchor(0) -> true, t.is_anchor(1) -> false
    [[nodiscard]] constexpr bool is_anchor(std::uint32_t r) const noexcept { return m_trie.is_root(r); }
    /// Whether `r` has a pathlib name: not an anchor and not an anchor-part
    /// segment (a drive, a share).                           t.has_name(3) -> true, t.has_name(0) -> false
    [[nodiscard]] constexpr bool has_name(std::uint32_t r) const noexcept {
        return !is_anchor(r) && !(m_trie.key(r) & anchor_bit);
    }
    /// pathlib's `name`: the final component, "" for an anchor or an
    /// anchor-part row.                                      t.name(3) -> "c.yaml", t.name(0) -> ""
    [[nodiscard]] constexpr std::string_view name(std::uint32_t r) const noexcept {
        return has_name(r) ? m_segments[m_trie.key(r)] : std::string_view{};
    }
    /// The segment id behind `r`'s key, anchor bit stripped.  t.name_id(3) -> 3 (segment "c.yaml")
    [[nodiscard]] constexpr std::uint32_t name_id(std::uint32_t r) const noexcept { return m_trie.key(r) & ~anchor_bit; }
    /// pathlib's parent: the row above, except that an anchor (or the "."
    /// row) is its own parent — `PurePath("/").parent == PurePath("/")`.
    ///
    ///     t.parent(3) -> 2     t.parent(1) -> 0     t.parent(0) -> 0     t.parent(6) -> 5
    [[nodiscard]] constexpr std::uint32_t parent(std::uint32_t r) const noexcept { return has_name(r) ? m_trie.parent(r) : r; }
    /// Named components below the anchor (a drive or share row is part of
    /// the anchor and not counted).                          t.depth(3) -> 3, t.depth(6) -> 1, t.depth(0) -> 0
    [[nodiscard]] constexpr std::uint32_t depth(std::uint32_t r) const noexcept {
        std::uint32_t d = 0;
        for (; !is_anchor(r); r = m_trie.parent(r)) d += (m_trie.key(r) & anchor_bit) ? 0u : 1u;
        return d;
    }
    /// The anchor row of `r`'s chain.                         t.anchor_of(3) -> 0, t.anchor_of(6) -> 5
    [[nodiscard]] constexpr std::uint32_t anchor_of(std::uint32_t r) const noexcept { return m_trie.root_of(r); }
    /// The anchor of `r`'s chain, decoded (a walk up, then anchor_at).
    ///
    ///     t.anchor(3) -> {absolute false, has_authority false, ""}
    ///     t.anchor(6) -> {absolute true,  has_authority false, ""}
    [[nodiscard]] constexpr anchor_info anchor(std::uint32_t r) const noexcept { return anchor_at(anchor_of(r)); }
    /// Decodes an anchor row's key (no walk): the flags byte, then the
    /// authority text after it.                              t.anchor_at(5) -> {true, false, ""}
    [[nodiscard]] constexpr anchor_info anchor_at(std::uint32_t anchor_row) const noexcept {
        const std::string_view key = m_segments[m_trie.key(anchor_row)];
        const auto flags = static_cast<unsigned char>(key[0]);
        return {(flags & 1u) != 0, (flags & 2u) != 0, key.substr(1)};
    }
    /// Ancestors closest first, basic_file::parents' rule: the empty
    /// relative parent "." is not listed, while an anchored path ends at its
    /// anchor, which is.
    ///
    ///     t.parents(3) -> [2, 1]       a/b/c.yaml: a/b, a        (not ".")
    ///     t.parents(6) -> [5]          /x: /
    ///     t.parents(1) -> []           a: nothing but "."
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

    /// f(rows, n): the chain LEAF FIRST (rows[0] is r, rows[n - 1] the
    /// anchor row) — the trie's walk, exposed so an adapter can read a
    /// whole chain in one pass.                              t.with_chain(3, f) -> f([3, 2, 1, 0], 4)
    template <class F>
    constexpr decltype(auto) with_chain(std::uint32_t r, F&& f) const {
        return m_trie.with_chain(r, static_cast<F&&>(f));
    }
    /// f(row) for every row from the anchor down to r, root first.
    ///                                                        t.for_chain(3, g) -> g(0), g(1), g(2), g(3)
    template <class F>
    constexpr void for_chain(std::uint32_t r, F& f) const {
        m_trie.for_chain(r, f);
    }

    // ── the value, its hash, its text ─────────────────────────────────────

    /// The value of a row: the uri file(text) holds for the same path,
    /// rebuilt from the anchor flags and the segments (one walk, one
    /// allocation per segment). `fileview.to_file()` and the flyweight
    /// store's cold path use it; proven equal to the parser's uri in
    /// tests/static/pathlike_core_proofs.cpp.
    ///
    ///     t.value(3)   -> uri{scheme "file", absolute false, segments ["a", "b", "c.yaml"]}
    ///     t.value(6)   -> uri{absolute true, segments ["x"]}
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

    /// == basic_file::hash_value() of value(r), without building the value:
    /// the same FNV-1a stream (utils/hash.h) over the authority, the two
    /// flag bits and each segment, read straight from the arena. What lets
    /// a view hash like a file, so `{fileview: ...}[file]` works.
    ///
    ///     t.hash(3) == file("a/b/c.yaml").hash_value()   -> true (a proof)
    ///     t.hash(3) == t.hash(4)                         -> false
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

    /// The anchor as a uri holding only the anchor-part segments: what a
    /// strategy's anchor()/is_absolute()/is_anchored() look at (nothing
    /// further). A drive or share row's text is in it; ordinary segments
    /// are not.
    ///
    ///     t.head_of(6)   -> uri{absolute true, segments []}
    ///     windows: head_of(row of C:\a\b) -> uri{absolute true, segments ["C:"]}
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
    /// pathlib's is_absolute(): rooted on POSIX; rooted AND on a drive or
    /// share on Windows (so "\\abs" is not absolute there, "C:\\abs" is).
    ///
    ///     t.is_absolute<posix_strategy>(6) -> true     t.is_absolute<posix_strategy>(3) -> false
    template <class Strategy>
    [[nodiscard]] constexpr bool is_absolute(std::uint32_t r) const { return Strategy::is_absolute(head_of(r)); }

    /// pathlib's str() of a row — Strategy::render()'s rule (the anchor,
    /// then the components joined; "." when empty) without building the
    /// value: one walk, the text written into a string sized up front.
    ///
    ///     t.render<posix_strategy>(3) -> "a/b/c.yaml"
    ///     t.render<posix_strategy>(6) -> "/x"
    ///     t.render<posix_strategy>(0) -> "."
    ///
    /// Cost: about 80 ns per path at 1M paths (benchmarks/pathset_prototype.py,
    /// the to_list row), 35% under the two-string version it replaced.
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

    // ── the parts, for accounting and the adapters ────────────────────────

    /// The segment interner (every distinct component once).   t.segments().size() -> 8 in the example
    [[nodiscard]] constexpr const Interner& segments() const noexcept { return m_segments; }
    /// The trie of rows.
    [[nodiscard]] constexpr const trie& rows() const noexcept { return m_trie; }
    /// The bytes the table holds, exactly: the interner's plus the trie's
    /// (PathSet.stats()["table_bytes"]; ~100 per path at 1M paths).
    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return m_segments.bytes() + m_trie.bytes(); }

private:
    /// Strategy::anchor() of a bare root: "/" or "" on POSIX, "\\" or "" on
    /// Windows — the prefix of every path with no authority and no
    /// anchor-part segments.
    template <class Strategy>
    [[nodiscard]] static constexpr std::string plain_anchor(bool absolute) {
        uri u;
        u.absolute = absolute;
        return Strategy::anchor(u);
    }
    /// The run-time cache of the two plain anchors (a static may not live in a
    /// constexpr function under C++20, hence the separate, ordinary helper).
    template <class Strategy>
    [[nodiscard]] static const std::string& cached_plain_anchor(bool absolute) {
        static const std::string abs_text = plain_anchor<Strategy>(true);
        static const std::string rel_text = plain_anchor<Strategy>(false);
        return absolute ? abs_text : rel_text;
    }

    // The tokenise() sinks: one descends adding rows, the other only looks.
    // A strategy's tokenise(text, sink) calls sink.anchor(...) once, then
    // sink.segment(s) per component; `cur` is the row reached so far.
    struct inserter {
        basic_path_table& t;
        std::uint32_t cur = none;
        std::size_t anchor_segs = 0;
        std::size_t depth = 0;
        /// The anchor: its row becomes `cur`; the first `anchor_segments`
        /// segments to come belong to it (a drive, a share, the "//" root).
        constexpr void anchor(bool absolute, bool has_authority, std::string_view authority, std::size_t anchor_segments) {
            anchor_segs = anchor_segments;
            cur = t.anchor_row(absolute, has_authority, authority);
        }
        /// One component: interned, flagged if it is an anchor part, then
        /// the child of `cur` by that key (created when absent).
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
        /// One component, looked up (never interned); once `cur` is `none`
        /// the rest of the walk is a no-op.
        constexpr void segment(std::string_view s) {
            const bool in_anchor = depth++ < anchor_segs;
            if (cur == none) return;
            const std::uint32_t id = t.m_segments.find(s);
            cur = id == Interner::npos ? none : t.m_trie.find_child(cur, id | (in_anchor ? anchor_bit : 0u));
        }
    };

    /// A parsed value through the sink interface (what a strategy's generic
    /// tokenise() does): the uri's anchor flags, then each segment.
    template <class Strategy, class Sink>
    static constexpr void feed(const uri& u, Sink& sink) {
        sink.anchor(u.absolute, u.has_authority, u.authority, Strategy::anchor_segments(u));
        for (const std::string& s : u.segments) sink.segment(s);
    }

    /// The anchor key's flags byte: 0x40 | absolute | has_authority << 1
    /// ("@", "A", "B", "C") — printable, so an anchor key reads as text in
    /// the interner, and never a valid segment start.
    [[nodiscard]] static constexpr char anchor_flags(bool absolute, bool has_authority) noexcept {
        return static_cast<char>(0x40 | (absolute ? 1 : 0) | (has_authority ? 2 : 0));
    }
    /// The four authority-less anchors are nearly every path's root: their
    /// rows are cached so the per-path cost is an array read, not an intern.
    [[nodiscard]] static constexpr std::size_t plain_index(bool absolute, bool has_authority) noexcept {
        return (absolute ? 1u : 0u) | (has_authority ? 2u : 0u);
    }
    /// The anchor row for these flags (and authority), created when absent.
    ///
    ///     anchor_row(false, false, "")   -> 0   (".", cached after the first call)
    ///     anchor_row(true, false, "")    -> 5   ("/")
    ///     anchor_row(true, true, "srv")  -> a root whose key is "C" + "srv"
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
    /// The anchor row for these flags, or `none` (an authority-less anchor
    /// is `none` until the first insert created it).
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
