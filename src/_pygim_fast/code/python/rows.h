#pragma once
// code/python/rows.h — a syntax tree as rows: written in postorder as the parser closes each node,
// then put in preorder by one pass at the end of the file (D24, #280).
//
//     row_writer w;                             // while parsing
//     auto first = w.mark();                    //   a node starts: its first descendant comes next
//     ... children close ...
//     w.close(kind::BinOp, first, start, end, op);   // it closes: its subtree is rows first..now
//     syntax_tree t = ... pack(w.rows()) ...    // at the end: preorder, each row knowing its parent
//
// Recursive descent closes a child before it knows its parent, so rows come in postorder: a node
// is the last row of its subtree, and its size says where the subtree began. Nothing is built and
// thrown away (a pegen parser makes 3.2 nodes per node kept, #276). One reverse pass then places
// every row at its preorder slot with a stack no deeper than the tree: the tree reads in source
// order, a node's children follow it, and `size` skips a subtree.
//
// A row: its kind, the field of its parent it fills (a slot of ast_tables.gen.h), `small` (an
// operator, a context, a flag, a small integer), `value` (an identifier's id in the name table, or
// a literal's index), its size, and its span as byte offsets into the source.
//
// CORE layer: pybind-free.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ast_tables.gen.h"

namespace pygim::code::python {

/// A value that is not there: an optional identifier left out, a literal no node has.
inline constexpr std::uint32_t no_value = 0xFFFFFFFFu;

class row_writer;
class node;

/// One node while the parser writes them: postorder.
class row {
public:
    constexpr row() noexcept = default;
    constexpr row(enum kind k, std::uint8_t field, std::uint16_t small, std::uint32_t size, std::uint32_t value, std::uint32_t start, std::uint32_t end) noexcept
        : m_kind(k), m_field(field), m_small(small), m_size(size), m_value(value), m_start(start), m_end(end) {}
    [[nodiscard]] constexpr enum kind kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr std::uint8_t field() const noexcept { return m_field; }
    [[nodiscard]] constexpr std::uint16_t small() const noexcept { return m_small; }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return m_value; }
    [[nodiscard]] constexpr std::uint32_t start() const noexcept { return m_start; }
    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return m_end; }

private:
    friend class row_writer;   // the parser's commands: slot, set_context
    friend class node;         // a name's id renumbered into the corpus's
    constexpr void set_field(std::uint8_t f) noexcept { m_field = f; }
    constexpr void set_small(std::uint16_t s) noexcept { m_small = s; }
    constexpr void set_value(std::uint32_t v) noexcept { m_value = v; }

    enum kind m_kind = kind::absent;
    std::uint8_t m_field = 0;
    std::uint16_t m_small = 0;
    std::uint32_t m_size = 1;
    std::uint32_t m_value = no_value;
    std::uint32_t m_start = 0;
    std::uint32_t m_end = 0;
};
static_assert(sizeof(row) == 20 && row(kind::Name, 2, 1, 1, 7, 3, 4).field() == 2 && row(kind::Name, 2, 1, 1, 7, 3, 4).value() == 7 &&
              row().kind() == kind::absent && row().size() == 1);

/// The rows of one file as the parser closes its nodes.
class row_writer {
public:
    /// Where the next row goes: what a node about to start records as its first.
    [[nodiscard]] constexpr std::uint32_t mark() const noexcept { return static_cast<std::uint32_t>(m_rows.size()); }

    /// Closes a node whose descendants are the rows from `first`: it is appended after them.
    constexpr std::uint32_t close(enum kind k, std::uint32_t first, std::uint32_t start, std::uint32_t end,
                                  std::uint16_t small = 0, std::uint32_t value = no_value) {
        const std::uint32_t at = mark();
        m_rows.emplace_back(k, 0, small, at - first + 1, value, start, end);
        return at;
    }
    /// A leaf of a scalar kind (identifier, cmpop, absent) in field `field`.
    constexpr std::uint32_t leaf(enum kind k, std::uint8_t field, std::uint16_t small, std::uint32_t value, std::uint32_t start = 0, std::uint32_t end = 0) {
        const std::uint32_t at = mark();
        m_rows.emplace_back(k, field, small, 1, value, start, end);
        return at;
    }
    /// Puts row `at` in its parent's field `field`.
    constexpr void slot(std::uint32_t at, std::uint8_t field) noexcept { m_rows[at].set_field(field); }
    /// Gives row `at`, an expression, the context `ctx` (Load 0, Store 1, Del 2), held in its small.
    constexpr void set_context(std::uint32_t at, std::uint16_t ctx) noexcept { m_rows[at].set_small(ctx); }
    /// A leaf put in at `at`, the rows from there moving up one: for a value found to come first
    /// only once what follows it is read (the text of `f"{x=}"`).
    constexpr void insert(std::uint32_t at, const row& r) { m_rows.insert(m_rows.begin() + at, r); }
    /// Drops every row from `n` on: a checkpoint's rewind.
    constexpr void truncate(std::uint32_t n) { m_rows.resize(n); }

    [[nodiscard]] constexpr const row& operator[](std::uint32_t i) const noexcept { return m_rows[i]; }
    [[nodiscard]] constexpr std::span<const row> rows() const noexcept { return m_rows; }
    /// Empties the rows and lets the room for more than `rows` go, keeping room for `rows`: a worker
    /// that once parsed a large file need not hold its rows for good, nor regrow them for the next.
    constexpr void keep_room_for(std::size_t rows) {
        if (m_rows.capacity() <= rows) return m_rows.clear();
        std::vector<row> kept;
        kept.reserve(rows);
        m_rows.swap(kept);
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_rows.size(); }
    /// The rows it has room for without growing.
    [[nodiscard]] constexpr std::size_t room() const noexcept { return m_rows.capacity(); }
    constexpr void clear() noexcept { m_rows.clear(); }

private:
    std::vector<row> m_rows;
};
static_assert([] {
    row_writer w;
    const std::uint32_t first = w.mark();
    w.close(kind::Name, w.mark(), 0, 1);                           // `x`, closed first
    const std::uint32_t call = w.close(kind::Call, first, 0, 3);   // `x()`, over it
    return call == 1 && w[call].size() == 2 && w.size() == 2;
}());
static_assert([] {
    row_writer w;
    for (int i = 0; i < 40; ++i) w.leaf(kind::absent, 0, 0, no_value);
    w.keep_room_for(16);                     // a large file's room let go, a smaller one's kept
    const bool shrunk = w.size() == 0 && w.room() == 16;
    w.keep_room_for(64);                     // under the bound: emptied, nothing let go
    return shrunk && w.size() == 0 && w.room() == 16;
}());

/// One node of a finished tree: preorder, with its parent's index.
class node {
public:
    constexpr node() noexcept = default;
    constexpr node(const row& r, std::uint32_t parent) noexcept : m_row(r), m_parent(parent) {}
    [[nodiscard]] constexpr enum kind kind() const noexcept { return m_row.kind(); }
    [[nodiscard]] constexpr std::uint8_t field() const noexcept { return m_row.field(); }
    [[nodiscard]] constexpr std::uint16_t small() const noexcept { return m_row.small(); }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return m_row.size(); }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return m_row.value(); }
    [[nodiscard]] constexpr std::uint32_t start() const noexcept { return m_row.start(); }
    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return m_row.end(); }
    /// The parent's index; no_value for the root.
    [[nodiscard]] constexpr std::uint32_t parent() const noexcept { return m_parent; }
    constexpr void set_value(std::uint32_t v) noexcept { m_row.set_value(v); }

private:
    row m_row;
    std::uint32_t m_parent = no_value;
};
static_assert(sizeof(node) == 24 && node(row(kind::Name, 2, 1, 1, 7, 3, 4), 5).parent() == 5 && node(row(kind::Name, 2, 1, 1, 7, 3, 4), 5).end() == 4);

namespace detail {

// The room a placed row leaves its children: where its subtree starts in postorder, its own
// preorder index, and the end of the room its children not yet placed have (filled from the end).
class room {
public:
    constexpr room(std::uint32_t first, std::uint32_t at, std::uint32_t end) noexcept : m_first(first), m_at(at), m_free(end) {}

    // Whether postorder row `i` is in this subtree.
    [[nodiscard]] constexpr bool holds(std::uint32_t i) const noexcept { return i >= m_first; }
    // The owner's preorder index: the parent of the rows placed in it.
    [[nodiscard]] constexpr std::uint32_t at() const noexcept { return m_at; }
    // The preorder index of the next child, `size` rows: the end of the room left.
    constexpr std::uint32_t place(std::uint32_t size) noexcept { return m_free -= size; }

private:
    std::uint32_t m_first;
    std::uint32_t m_at;
    std::uint32_t m_free;
};
static_assert(room(2, 0, 5).place(3) == 2 && room(2, 0, 5).holds(2) && !room(2, 0, 5).holds(1));

}  // namespace detail

/// Postorder rows to preorder nodes, in one reverse pass: each row goes to the end of the room its
/// parent has left, and a row with children leaves room for them in turn. The roots' room holds
/// every row, under no parent. The nodes are in memory from `alloc` (a corpus's arena).
template <class Alloc = std::allocator<node>>
[[nodiscard]] constexpr std::vector<node, Alloc> pack(std::span<const row> rows, const Alloc& alloc = Alloc()) {
    std::vector<node, Alloc> out(rows.size(), alloc);
    std::vector<detail::room> open{detail::room(0, no_value, static_cast<std::uint32_t>(rows.size()))};
    for (std::size_t k = rows.size(); k-- > 0;) {
        const auto i = static_cast<std::uint32_t>(k);
        while (!open.back().holds(i)) open.pop_back();
        const row& r = rows[i];
        const std::uint32_t at = open.back().place(r.size());
        out[at] = node(r, open.back().at());
        if (r.size() > 1) open.emplace_back(i + 1 - r.size(), at, at + r.size());
    }
    return out;
}

namespace detail {
// `a + b * c` closed in postorder: a, b, c, b*c, a+(b*c) — packed it reads in source order.
[[nodiscard]] constexpr bool pack_reads_in_source_order() {
    row_writer w;
    const std::uint32_t a = w.close(kind::Name, w.mark(), 0, 1);
    const std::uint32_t first_bc = w.mark();
    const std::uint32_t b = w.close(kind::Name, w.mark(), 4, 5);
    const std::uint32_t c = w.close(kind::Name, w.mark(), 8, 9);
    const std::uint32_t mul = w.close(kind::BinOp, first_bc, 4, 9);
    const std::uint32_t add = w.close(kind::BinOp, a, 0, 9);
    w.slot(b, slot::BinOp::left);
    w.slot(c, slot::BinOp::right);
    w.slot(mul, slot::BinOp::right);
    const std::vector<node> t = pack(w.rows());
    return add == 4 && t.size() == 5 && t[0].size() == 5 && t[0].parent() == no_value && t[1].start() == 0 && t[1].parent() == 0 &&
           t[2].kind() == kind::BinOp && t[2].parent() == 0 && t[2].size() == 3 && t[3].start() == 4 && t[3].parent() == 2 &&
           t[4].start() == 8 && t[4].parent() == 2 && t[4].field() == slot::BinOp::right;
}
}  // namespace detail
static_assert(detail::pack_reads_in_source_order());

/// A part of a string's value as written: a span of the source and how to read it.
namespace piece_flag {
inline constexpr std::uint8_t raw = 1;        // no escapes
inline constexpr std::uint8_t bytes = 2;      // a bytes literal's
inline constexpr std::uint8_t fstring = 4;    // f-/t-string text: {{ and }} are braces
inline constexpr std::uint8_t verbatim = 8;   // the source text as it is: f"{x=}"'s "x=", t"{x}"'s "x"
inline constexpr std::uint8_t code = 16;      // with verbatim: an expression's text, its comments left out
}  // namespace piece_flag

/// One piece of a literal: the source between `start` and `end`, read as its flags say.
class literal_piece {
public:
    constexpr literal_piece() noexcept : m_start(0), m_end(0), m_flags(0) {}
    constexpr literal_piece(std::uint32_t start, std::uint32_t end, std::uint8_t flags) noexcept : m_start(start), m_end(end), m_flags(flags) {}
    [[nodiscard]] constexpr std::uint32_t start() const noexcept { return m_start; }
    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return m_end; }
    [[nodiscard]] constexpr std::uint8_t flags() const noexcept { return m_flags; }
    [[nodiscard]] constexpr bool has(std::uint8_t f) const noexcept { return (m_flags & f) != 0; }

private:
    std::uint32_t m_start, m_end;
    std::uint8_t m_flags;
};
static_assert(literal_piece(3, 9, piece_flag::raw).has(piece_flag::raw) && !literal_piece(3, 9, piece_flag::raw).has(piece_flag::bytes));

/// A string's value: consecutive pieces (implicitly concatenated strings are one value).
class literal {
public:
    constexpr literal() noexcept : m_first(0), m_count(0) {}
    constexpr literal(std::uint32_t first, std::uint32_t count) noexcept : m_first(first), m_count(count) {}
    [[nodiscard]] constexpr std::uint32_t first() const noexcept { return m_first; }
    [[nodiscard]] constexpr std::uint32_t count() const noexcept { return m_count; }

private:
    std::uint32_t m_first, m_count;
};
static_assert(literal(4, 2).first() == 4 && literal(4, 2).count() == 2);   // "a" "b": pieces 4 and 5

/// What kind of value a Constant node holds: its small's low bits; 0x10 marks a u prefix.
enum class constant_kind : std::uint8_t { none, true_, false_, ellipsis, integer, floating, imaginary, string, bytes };
/// How many constant kinds there are: bytes is the last.
inline constexpr std::size_t constant_kind_count = static_cast<std::size_t>(constant_kind::bytes) + 1;
inline constexpr std::uint16_t unicode_prefix = 0x10;
[[nodiscard]] constexpr constant_kind constant_of(std::uint16_t small) noexcept { return static_cast<constant_kind>(small & 0x0F); }
static_assert(constant_of(static_cast<std::uint16_t>(constant_kind::string) | unicode_prefix) == constant_kind::string);   // u"x"

}  // namespace pygim::code::python
