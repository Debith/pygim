#pragma once
// code/python/grammar/vocabulary.h — what every part of the grammar speaks in: a closed
// expression as its parent sees it, the indices a row's small holds for a context or a unary
// operator, the depth guard, and the walks over the subtrees a run of rows holds.
//
// CORE layer: pybind-free.

#include <cstdint>
#include <string_view>

#include "../ast_tables.gen.h"
#include "../rows.h"

namespace pygim::code::python::grammar {

/// How deep expressions and statements nest, beyond the lexer's own limits.
inline constexpr int max_depth = 1000;

/// A closed expression: its root row, where its subtree begins, and its span with any
/// parentheses around it (a parent's span starts at its first token, a parenthesis included).
class expr {
public:
    constexpr expr(std::uint32_t row, std::uint32_t first, std::uint32_t start, std::uint32_t end, bool parenthesized = false) noexcept
        : m_row(row), m_first(first), m_start(start), m_end(end), m_parenthesized(parenthesized) {}
    [[nodiscard]] constexpr std::uint32_t row() const noexcept { return m_row; }
    [[nodiscard]] constexpr std::uint32_t first() const noexcept { return m_first; }
    [[nodiscard]] constexpr std::uint32_t start() const noexcept { return m_start; }
    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return m_end; }
    [[nodiscard]] constexpr bool parenthesized() const noexcept { return m_parenthesized; }
    /// The same expression inside parentheses from `start` to `end`.
    [[nodiscard]] constexpr expr in_parentheses(std::uint32_t start, std::uint32_t end) const noexcept { return {m_row, m_first, start, end, true}; }

private:
    std::uint32_t m_row, m_first, m_start, m_end;
    bool m_parenthesized;
};

static_assert(expr(3, 1, 10, 12).in_parentheses(9, 13).parenthesized());

// A context's index in expr_context and a unary operator's in unaryop: what a row's small holds.
inline constexpr std::uint16_t load = member_index(kind::Load, family::expr_context), store = member_index(kind::Store, family::expr_context),
                               del = member_index(kind::Del_, family::expr_context);
inline constexpr std::uint16_t unary_invert = member_index(kind::Invert, family::unaryop), unary_not = member_index(kind::Not_, family::unaryop),
                               unary_uadd = member_index(kind::UAdd, family::unaryop), unary_usub = member_index(kind::USub, family::unaryop);

/// A depth guard: expressions and blocks nest at most max_depth deep, the refusal naming the token
/// that would go one deeper.
template <class Cursor>
class nesting {
public:
    nesting(int& depth, Cursor& cursor) : m_depth(depth) {
        // checked before the count moves: a guard whose constructor throws has no destructor to
        // undo it, and a speculative parse rewound after it would run the rest one level short
        if (m_depth >= max_depth) cursor.fail_here("too many nested expressions");
        ++m_depth;
    }
    ~nesting() { --m_depth; }
    nesting(const nesting&) = delete;
    nesting& operator=(const nesting&) = delete;

private:
    int& m_depth;
};

/// What a node is called in a refusal: `cannot assign to function call`.
[[nodiscard]] constexpr std::string_view describe_kind(const row& r) noexcept {
    switch (r.kind()) {
        case kind::Call: return "function call";
        case kind::Constant: return "literal";
        case kind::Tuple: return "tuple";
        case kind::List: return "list";
        case kind::Starred: return "starred";
        case kind::NamedExpr: return "named expression";
        case kind::Compare: return "comparison";
        case kind::Lambda: return "lambda";
        case kind::IfExp: return "conditional expression";
        case kind::JoinedStr: return "f-string expression";
        case kind::TemplateStr: return "t-string expression";
        case kind::Await: return "await expression";
        case kind::Yield: case kind::YieldFrom: return "yield expression";
        case kind::GeneratorExp: return "generator expression";
        case kind::ListComp: return "list comprehension";
        case kind::SetComp: return "set comprehension";
        case kind::DictComp: return "dict comprehension";
        case kind::Dict: return "dict literal";
        case kind::Set: return "set display";
        default: return "expression";
    }
}

static_assert(describe_kind(row(kind::Call, 0, 0, 1, no_value, 0, 4)) == "function call");

/// The roots of the subtrees that fill rows [first, end), last to first.
template <class Fn>
void for_each_root(const row_writer& rows, std::uint32_t first, std::uint32_t end, Fn&& fn) {
    for (std::uint32_t c = end; c > first;) {
        const std::uint32_t r = c - 1;
        fn(r);
        c = r + 1 - rows[r].size();
    }
}

/// The roots of the subtrees that fill rows [first, end), first to last: a line's few statements.
template <class Fn>
void for_each_root_in_order(const row_writer& rows, std::uint32_t first, std::uint32_t end, Fn&& fn) {
    if (end == first) return;
    const std::uint32_t r = end - 1;
    for_each_root_in_order(rows, first, r + 1 - rows[r].size(), fn);
    fn(r);
}

}  // namespace pygim::code::python::grammar
