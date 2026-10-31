#pragma once
// code/python/tree.h — one parsed file: its nodes in preorder, its text, its lines, the pieces of
// its strings. What a parse leaves; what views read.
//
//     parse_outcome r = parse_python(std::move(bytes), names, scratch);   // a tree, or a parse_failure
//     const syntax_tree& t = std::get<syntax_tree>(r);
//     t.size(), t[0].kind() == kind::Module
//     for (std::uint32_t c : t.children(0)) ...              // the module's statements, in order
//     t.position_of(t[c].start())                            // line and column, as ast counts them
//
// Identifiers are ids into a name table the tree does not own (a corpus shares one across its
// files); a literal is decoded from the text when asked (literals.h).
//
// A tree's nodes, text and lines are where its bytes were read (an arena_string's memory): a
// corpus's in its workers' chunks (D61, D69), anything else's on the heap. Its strings' pieces and
// literals are moved from the parse's scratch, on the heap (output.h says why).
//
// CORE layer: pybind-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../../mapping/intern.h"
#include "../../utils/chunk_arena.h"
#include "../../wiring/registry/core.h"
#include "parser.h"
#include "rows.h"
#include "source.h"

namespace pygim::code::python {

/// Whether a row of kind k is a node as ast shows one: not an identifier, an operator of a
/// comparison, or the placeholder of a missing value (a dict key, a keyword default) — those are
/// field values, read through the node that holds them.
[[nodiscard]] constexpr bool is_node(kind k) noexcept { return k != kind::identifier && k != kind::cmpop && k != kind::absent; }
static_assert(is_node(kind::Name) && !is_node(kind::absent) && !is_node(kind::cmpop) && !is_node(kind::identifier));

namespace detail {
// Each node kind by its name, as ast calls it: the bucketed engine, as the keywords are.
using kind_table = core::StaticWordRegistry<kind, kind_count>;
inline constexpr kind_table kinds_by_name = [] {
    kind_table t;
    for (std::size_t k = 0; k < kind_count; ++k) {
        if (is_node(static_cast<kind>(k))) t.register_or_override(kinds[k].name(), static_cast<kind>(k), false);
    }
    return t;
}();
}  // namespace detail

/// The node kind ast calls `name` ("Name", "FunctionDef"); none for another word, and for the
/// field values that are no node (identifier, cmpop, absent).
[[nodiscard]] constexpr std::optional<kind> kind_named(std::string_view name) noexcept {
    const kind k = detail::kinds_by_name.value_or(name, kind::absent);
    return k == kind::absent ? std::nullopt : std::optional<kind>(k);
}
static_assert(kind_named("Name") == kind::Name && kind_named("AsyncFunctionDef") == kind::AsyncFunctionDef &&
              !kind_named("identifier") && !kind_named("Nmae") && !kind_named(""));

namespace detail {
// Whether a node of kind k holds an identifier in its value: `identifier` rows, and a kind whose
// first value field is an identifier (Name.id, FunctionDef.name ...).
[[nodiscard]] constexpr bool kind_holds_identifier(kind k) noexcept {
    if (k == kind::identifier) return true;
    if (k == kind::absent || k == kind::cmpop) return false;
    for (const field_info& f : info(k).fields()) {
        if (f.storage() == storage::value) return f.shape() == shape::identifier || f.shape() == shape::optional_identifier;
    }
    return false;
}
static_assert(kind_holds_identifier(kind::Name) && kind_holds_identifier(kind::FunctionDef) && !kind_holds_identifier(kind::Constant));
inline constexpr std::array<bool, kind_count> identifier_kinds = [] {
    std::array<bool, kind_count> t{};
    for (std::size_t k = 0; k < kind_count; ++k) t[k] = kind_holds_identifier(static_cast<kind>(k));
    return t;
}();
static_assert(identifier_kinds[static_cast<std::size_t>(kind::Name)] && !identifier_kinds[static_cast<std::size_t>(kind::Constant)]);
}  // namespace detail

/// The children of node `parent` among preorder nodes: their indices, each the one after the
/// previous child's subtree.
class child_range {
public:
    class iterator {
    public:
        constexpr iterator(std::span<const node> nodes, std::uint32_t at) noexcept : m_nodes(nodes), m_at(at) {}
        [[nodiscard]] constexpr std::uint32_t operator*() const noexcept { return m_at; }
        constexpr iterator& operator++() noexcept {
            m_at += m_nodes[m_at].size();
            return *this;
        }
        [[nodiscard]] constexpr bool operator==(const iterator& o) const noexcept { return m_at == o.m_at; }

    private:
        std::span<const node> m_nodes;
        std::uint32_t m_at;
    };
    constexpr child_range(std::span<const node> nodes, std::uint32_t parent) noexcept : m_nodes(nodes), m_parent(parent) {}
    [[nodiscard]] constexpr iterator begin() const noexcept { return {m_nodes, m_parent + 1}; }
    [[nodiscard]] constexpr iterator end() const noexcept { return {m_nodes, m_parent + m_nodes[m_parent].size()}; }

private:
    std::span<const node> m_nodes;
    std::uint32_t m_parent;
};
static_assert([] {   // f(a, b.c): Call over Name f, Name a, Attribute over Name b
    const std::array<node, 5> n{node(row(kind::Call, 0, 0, 5, no_value, 0, 9), no_value), node(row(kind::Name, 0, 0, 1, 0, 0, 1), 0),
                                node(row(kind::Name, 1, 0, 1, 1, 2, 3), 0), node(row(kind::Attribute, 1, 0, 2, 2, 5, 8), 0),
                                node(row(kind::Name, 0, 0, 1, 3, 5, 6), 3)};
    std::uint32_t sum = 0, count = 0;
    for (std::uint32_t c : child_range(n, 0)) sum += c, ++count;
    return count == 3 && sum == 1 + 2 + 3;
}());

/// One file's syntax tree.
class syntax_tree {
public:
    syntax_tree() = default;
    /// The tree of what a parse of `src` wrote, packed to preorder; its nodes where src's text is.
    syntax_tree(source_text&& src, parse_output&& out)
        : m_nodes(pack(out.rows().rows(), arena_allocator<node>(src.arena()))), m_pieces(out.take_pieces()), m_literals(out.take_literals()),
          m_lines(std::move(src).release_lines()), m_text(std::move(src).release()) {}

    [[nodiscard]] std::size_t size() const noexcept { return m_nodes.size(); }
    [[nodiscard]] const node& operator[](std::uint32_t i) const noexcept { return m_nodes[i]; }
    /// Every node, in preorder.
    [[nodiscard]] std::span<const node> nodes() const noexcept { return m_nodes; }
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }
    [[nodiscard]] position position_of(std::uint32_t offset) const noexcept { return source_text::position_in(m_lines, offset); }
    [[nodiscard]] const literal& literal_at(std::uint32_t i) const noexcept { return m_literals[i]; }
    [[nodiscard]] const literal_piece& piece_at(std::uint32_t i) const noexcept { return m_pieces[i]; }

    /// The children of node `i`, in source order: their indices.
    [[nodiscard]] child_range children(std::uint32_t i) const noexcept { return {m_nodes, i}; }

    /// Renumbers the identifiers of the nodes by `map` (a worker's ids to a corpus's).
    void remap_names(const std::vector<std::uint32_t>& map) noexcept {
        for (node& n : m_nodes) {
            if (holds_identifier(n) && n.value() != no_value) n.set_value(map[n.value()]);
        }
    }
    /// Whether a node's value is an identifier (and not a literal's index): its kind says, read from
    /// a table made at compile time (a walk of the kind's fields per node was half of a corpus's merge).
    [[nodiscard]] static constexpr bool holds_identifier(const node& n) noexcept { return detail::identifier_kinds[static_cast<std::size_t>(n.kind())]; }

    /// What the tree holds, in bytes.
    [[nodiscard]] std::size_t bytes() const noexcept {
        return m_nodes.capacity() * sizeof(node) + m_pieces.capacity() * sizeof(literal_piece) + m_literals.capacity() * sizeof(literal) +
               m_lines.capacity() * sizeof(std::uint32_t) + m_text.capacity();
    }

private:
    arena_vector<node> m_nodes;
    std::vector<literal_piece> m_pieces;
    std::vector<literal> m_literals;
    source_text::lines m_lines;
    arena_string m_text;
};
static_assert(syntax_tree::holds_identifier(node(row(kind::Name, 0, 0, 1, 7, 0, 1), no_value)) &&
              !syntax_tree::holds_identifier(node(row(kind::Constant, 0, 0, 1, 7, 0, 1), no_value)));

/// What a node's field stored in its small holds, as ast shows it: one reading, which the dump,
/// the bindings and similarity's frontend each only translate.
enum class small_meaning : std::uint8_t {
    member,          // an operator or a context: the kind family() + small (BinOp.op, Name.ctx)
    singleton,       // MatchSingleton.value: a constant_kind, none, true_ or false_
    string_prefix,   // Constant.kind: 'u' when the literal was written u"...", else None
    conversion,      // FormattedValue's and Interpolation's conversion: -1 for none, else a character's code
    number,          // ImportFrom.level, AnnAssign.simple, comprehension.is_async
};
[[nodiscard]] constexpr small_meaning meaning_of(kind k, const field_info& f) noexcept {
    if (f.shape() == shape::operator_) return small_meaning::member;
    if (f.shape() == shape::constant) return small_meaning::singleton;
    if (k == kind::Constant) return small_meaning::string_prefix;
    if (k == kind::FormattedValue || k == kind::Interpolation) return small_meaning::conversion;
    return small_meaning::number;
}
static_assert(meaning_of(kind::BinOp, info(kind::BinOp).fields()[slot::BinOp::op]) == small_meaning::member &&
              meaning_of(kind::MatchSingleton, info(kind::MatchSingleton).fields()[slot::MatchSingleton::value]) == small_meaning::singleton &&
              meaning_of(kind::Constant, info(kind::Constant).fields()[slot::Constant::kind]) == small_meaning::string_prefix &&
              meaning_of(kind::FormattedValue, info(kind::FormattedValue).fields()[slot::FormattedValue::conversion]) == small_meaning::conversion &&
              meaning_of(kind::ImportFrom, info(kind::ImportFrom).fields()[slot::ImportFrom::level]) == small_meaning::number);

/// A member's kind (an operator field's, or a leaf's of an `operators` field): Add for BinOp.op 0.
[[nodiscard]] constexpr kind member_of(const field_info& f, std::uint16_t small) noexcept { return static_cast<kind>(f.family() + small); }
static_assert(member_of(info(kind::BinOp).fields()[slot::BinOp::op], 0) == kind::Add);
/// A conversion as ast holds it: -1 for none, else the character's code ('r' 114).
[[nodiscard]] constexpr int conversion_of(std::uint16_t small) noexcept { return static_cast<std::int16_t>(small); }
static_assert(conversion_of(0xFFFF) == -1 && conversion_of('r') == 'r');
/// Whether a Constant was written with the u prefix: its kind is 'u'.
[[nodiscard]] constexpr bool has_unicode_prefix(std::uint16_t small) noexcept { return (small & unicode_prefix) != 0; }
static_assert(has_unicode_prefix(static_cast<std::uint16_t>(constant_kind::string) | unicode_prefix) && !has_unicode_prefix(static_cast<std::uint16_t>(constant_kind::string)));

/// Why a source has no tree: what CPython would say, and where.
class parse_failure {
public:
    parse_failure(std::string message, std::optional<position> at, error_place place)
        : m_message(std::move(message)), m_at(at), m_place(place) {}
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
    /// Where in the text, if the error has a place there.
    [[nodiscard]] const std::optional<position>& at() const noexcept { return m_at; }
    [[nodiscard]] error_place place() const noexcept { return m_place; }

private:
    std::string m_message;
    std::optional<position> m_at;
    error_place m_place;
};

/// A source's tree, or why it has none.
using parse_outcome = std::variant<syntax_tree, parse_failure>;

/// A file's bytes (or, `decoded`, a str's UTF-8: no BOM, no coding cookie) parsed with `scratch`,
/// its identifiers interned into `names`: the tree, or why it is not Python and where. The tree is
/// where the bytes are: in the arena they were read into, or on the heap. Any other error
/// (bad_alloc) is thrown. The one parse every caller makes: the corpus, similarity.
template <mapping::interner Names>
[[nodiscard]] parse_outcome parse_python(arena_string bytes, Names& names, parse_output& scratch, bool decoded = false) {
    std::optional<source_text> src;
    try {
        src.emplace(decoded ? source_text::from_text(std::move(bytes)) : source_text::from_bytes(std::move(bytes)));
        scratch.clear();
        basic_parser<stop_at_first, Names>(src->text(), scratch, names).module();
        return syntax_tree(std::move(*src), std::move(scratch));
    } catch (const syntax_error& e) {
        return parse_failure(e.message(), position_of(e, src ? &*src : nullptr), e.place());
    }
}

}  // namespace pygim::code::python
