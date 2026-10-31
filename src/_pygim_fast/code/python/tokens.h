#pragma once
// code/python/tokens.h — the tokens of Python 3.14 source: their kinds, what a kind is to the
// parser (a table per kind, built at compile time), and the keywords, looked up by first byte
// and length (core::StaticWordRegistry).
//
//     token t = lex.next();                     // {kind, flags, start, end}: no text, no value
//     t.kind() == token_kind::name              // the text is source[t.start(), t.end())
//     keyword_of("while") -> token_kind::kw_while, keyword_of("whilst") -> token_kind::name
//
// A token holds no value: its text is re-read from the source by its span, and a literal is
// decoded only when asked for (literals.h). Soft keywords (match, case, type) are kinds of their
// own; the parser turns them back into names where they are names (#280, after Ruff).
//
// CORE layer: pybind-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../wiring/registry/core.h"
#include "ast_tables.gen.h"

namespace pygim::code::python {

/// What a token is. Keywords, then the soft keywords, then operators and delimiters.
enum class token_kind : std::uint8_t {
    end_of_file, newline, indent, dedent,
    name, number, string, fstring_start, fstring_middle, fstring_end,
    // keywords, in alphabetical order (keyword_of's table is built from these names)
    kw_False, kw_None, kw_True, kw_and, kw_as, kw_assert, kw_async, kw_await, kw_break, kw_class,
    kw_continue, kw_def, kw_del, kw_elif, kw_else, kw_except, kw_finally, kw_for, kw_from, kw_global,
    kw_if, kw_import, kw_in, kw_is, kw_lambda, kw_nonlocal, kw_not, kw_or, kw_pass, kw_raise,
    kw_return, kw_try, kw_while, kw_with, kw_yield,
    // soft keywords: names everywhere but where their statement starts
    kw_match, kw_case, kw_type,
    // operators and delimiters
    lpar, rpar, lsqb, rsqb, lbrace, rbrace, colon, comma, semi, plus, minus, star, slash, vbar, amper,
    less, greater, equal, dot, percent, eqequal, notequal, lessequal, greaterequal, tilde, circumflex,
    leftshift, rightshift, doublestar, plusequal, minequal, starequal, slashequal, percentequal,
    amperequal, vbarequal, circumflexequal, leftshiftequal, rightshiftequal, doublestarequal,
    doubleslash, doubleslashequal, at, atequal, rarrow, ellipsis, colonequal, exclamation,
    count_
};
inline constexpr std::size_t token_kind_count = static_cast<std::size_t>(token_kind::count_);

/// A string token's prefix and quotes, and a number's form: the token's flags.
namespace token_flag {
inline constexpr std::uint8_t raw = 1, bytes = 2, unicode = 4, triple = 8, template_ = 16;   // strings, f-/t-string starts
inline constexpr std::uint8_t floating = 1, imaginary = 2;                                     // numbers
}  // namespace token_flag

/// One token: its kind, its flags and its span, byte offsets into the source.
class token {
public:
    constexpr token() noexcept = default;
    constexpr token(token_kind kind, std::uint8_t flags, std::uint32_t start, std::uint32_t end) noexcept
        : m_kind(kind), m_flags(flags), m_start(start), m_end(end) {}
    [[nodiscard]] constexpr token_kind kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr std::uint8_t flags() const noexcept { return m_flags; }
    [[nodiscard]] constexpr bool has(std::uint8_t flag) const noexcept { return (m_flags & flag) != 0; }
    [[nodiscard]] constexpr std::uint32_t start() const noexcept { return m_start; }
    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return m_end; }
    [[nodiscard]] constexpr std::string_view text(std::string_view source) const noexcept { return source.substr(m_start, m_end - m_start); }

private:
    token_kind m_kind = token_kind::end_of_file;
    std::uint8_t m_flags = 0;
    std::uint32_t m_start = 0;
    std::uint32_t m_end = 0;
};
static_assert(sizeof(token) == 12 && token(token_kind::name, 0, 3, 5).text("ab cde") == "cd");

// ── keywords ────────────────────────────────────────────────────────────────
namespace detail {

inline constexpr std::array<std::string_view, 38> keyword_names{
    "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class",
    "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
    "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
    "return", "try", "while", "with", "yield", "match", "case", "type"};

// Each keyword's token kind, as a compile-time registry over the bucketed engine: a word is
// compared only with the keywords of its first byte and length (at most three: `i` and 2 bytes).
static_assert(keyword_names.size() == static_cast<std::size_t>(token_kind::kw_type) - static_cast<std::size_t>(token_kind::kw_False) + 1);
using keyword_table = core::StaticWordRegistry<token_kind, keyword_names.size()>;
inline constexpr keyword_table keywords = [] {
    keyword_table t;
    for (std::size_t i = 0; i < keyword_names.size(); ++i)
        t.register_or_override(keyword_names[i], static_cast<token_kind>(static_cast<std::size_t>(token_kind::kw_False) + i), false);
    return t;
}();

}  // namespace detail

/// The keyword a word is, or token_kind::name.
[[nodiscard]] constexpr token_kind keyword_of(std::string_view word) noexcept {
    return detail::keywords.value_or(word, token_kind::name);
}
static_assert(keyword_of("while") == token_kind::kw_while && keyword_of("match") == token_kind::kw_match &&
              keyword_of("False") == token_kind::kw_False && keyword_of("whilst") == token_kind::name &&
              keyword_of("x") == token_kind::name && keyword_of("yield") == token_kind::kw_yield);

// ── what a kind is to the parser ────────────────────────────────────────────

/// What a token kind means to the parser: a binary operator and its strength, an augmented
/// assignment and its operator, a comparison, whether a name stands here, whether an expression
/// can start with it. One table, read by kind; built through the as_*() steps, each a copy with one
/// more fact.
class token_info {
public:
    constexpr token_info() = default;

    /// 0: not a binary operator; else its strength, 1 (|) .. 6 (* / // % @).
    [[nodiscard]] constexpr std::uint8_t binary_power() const noexcept { return m_binary_power; }
    /// The operator's index in ast's `operator` family (Add = 0).
    [[nodiscard]] constexpr std::uint8_t binary_op() const noexcept { return m_binary_op; }
    /// An augmented assignment: its operator's index + 1; else 0.
    [[nodiscard]] constexpr std::uint8_t aug_op() const noexcept { return m_aug_op; }
    /// A one-token comparison: its index in `cmpop` + 1; else 0. `is`, `is not`, `not in` are
    /// read by the parser itself (two tokens).
    [[nodiscard]] constexpr std::uint8_t compare_op() const noexcept { return m_compare_op; }
    /// A name, or a soft keyword standing as one.
    [[nodiscard]] constexpr bool name_like() const noexcept { return m_name_like; }
    /// An expression can begin with it.
    [[nodiscard]] constexpr bool starts_expression() const noexcept { return m_starts_expression; }

    [[nodiscard]] constexpr token_info as_binary(std::uint8_t power, std::uint8_t op) const noexcept {
        token_info t = *this;
        t.m_binary_power = power;
        t.m_binary_op = op;
        return t;
    }
    [[nodiscard]] constexpr token_info as_augmented(std::uint8_t op) const noexcept {
        token_info t = *this;
        t.m_aug_op = static_cast<std::uint8_t>(op + 1);
        return t;
    }
    [[nodiscard]] constexpr token_info as_comparison(std::uint8_t op) const noexcept {
        token_info t = *this;
        t.m_compare_op = static_cast<std::uint8_t>(op + 1);
        return t;
    }
    [[nodiscard]] constexpr token_info as_name() const noexcept {
        token_info t = *this;
        t.m_name_like = true;
        return t;
    }
    [[nodiscard]] constexpr token_info as_expression_start() const noexcept {
        token_info t = *this;
        t.m_starts_expression = true;
        return t;
    }

private:
    std::uint8_t m_binary_power = 0;
    std::uint8_t m_binary_op = 0;
    std::uint8_t m_aug_op = 0;
    std::uint8_t m_compare_op = 0;
    bool m_name_like = false;
    bool m_starts_expression = false;
};
static_assert(token_info().as_binary(5, 0).binary_power() == 5 && token_info().as_augmented(0).aug_op() == 1 && !token_info().name_like());

namespace detail {

// What a row's small holds for an operator, its index in ast's `operator` family, and for a
// comparison, in `cmpop`: from the generated table's order.
[[nodiscard]] constexpr std::uint8_t binary_index(kind k) noexcept { return member_index(k, family::operator_); }
[[nodiscard]] constexpr std::uint8_t compare_index(kind k) noexcept { return member_index(k, family::cmpop); }
static_assert(binary_index(kind::FloorDiv) == 12 && compare_index(kind::NotIn) == 9);

inline constexpr std::array<token_info, token_kind_count> token_table = [] {
    std::array<token_info, token_kind_count> t{};
    auto at = [&](token_kind k) -> token_info& { return t[static_cast<std::size_t>(k)]; };
    auto bin = [&](token_kind k, std::uint8_t power, kind op) { at(k) = at(k).as_binary(power, binary_index(op)); };
    bin(token_kind::vbar, 1, kind::BitOr);
    bin(token_kind::circumflex, 2, kind::BitXor);
    bin(token_kind::amper, 3, kind::BitAnd);
    bin(token_kind::leftshift, 4, kind::LShift);
    bin(token_kind::rightshift, 4, kind::RShift);
    bin(token_kind::plus, 5, kind::Add);
    bin(token_kind::minus, 5, kind::Sub);
    bin(token_kind::star, 6, kind::Mult);
    bin(token_kind::at, 6, kind::MatMult);
    bin(token_kind::slash, 6, kind::Div);
    bin(token_kind::doubleslash, 6, kind::FloorDiv);
    bin(token_kind::percent, 6, kind::Mod);
    auto aug = [&](token_kind k, kind op) { at(k) = at(k).as_augmented(binary_index(op)); };
    aug(token_kind::plusequal, kind::Add);
    aug(token_kind::minequal, kind::Sub);
    aug(token_kind::starequal, kind::Mult);
    aug(token_kind::atequal, kind::MatMult);
    aug(token_kind::slashequal, kind::Div);
    aug(token_kind::percentequal, kind::Mod);
    aug(token_kind::doublestarequal, kind::Pow);
    aug(token_kind::leftshiftequal, kind::LShift);
    aug(token_kind::rightshiftequal, kind::RShift);
    aug(token_kind::vbarequal, kind::BitOr);
    aug(token_kind::circumflexequal, kind::BitXor);
    aug(token_kind::amperequal, kind::BitAnd);
    aug(token_kind::doubleslashequal, kind::FloorDiv);
    auto cmp = [&](token_kind k, kind op) { at(k) = at(k).as_comparison(compare_index(op)); };
    cmp(token_kind::eqequal, kind::Eq);
    cmp(token_kind::notequal, kind::NotEq);
    cmp(token_kind::less, kind::Lt);
    cmp(token_kind::lessequal, kind::LtE);
    cmp(token_kind::greater, kind::Gt);
    cmp(token_kind::greaterequal, kind::GtE);
    cmp(token_kind::kw_in, kind::In_);
    for (token_kind k : {token_kind::name, token_kind::kw_match, token_kind::kw_case, token_kind::kw_type}) at(k) = at(k).as_name();
    for (token_kind k : {token_kind::name, token_kind::kw_match, token_kind::kw_case, token_kind::kw_type, token_kind::number,
                         token_kind::string, token_kind::fstring_start, token_kind::kw_False, token_kind::kw_None,
                         token_kind::kw_True, token_kind::kw_await, token_kind::kw_lambda, token_kind::kw_not,
                         token_kind::lpar, token_kind::lsqb, token_kind::lbrace, token_kind::plus, token_kind::minus,
                         token_kind::tilde, token_kind::ellipsis, token_kind::star, token_kind::kw_yield}) {
        at(k) = at(k).as_expression_start();
    }
    return t;
}();

}  // namespace detail

/// What `kind` is to the parser.
[[nodiscard]] constexpr const token_info& info(token_kind kind) noexcept { return detail::token_table[static_cast<std::size_t>(kind)]; }
static_assert(info(token_kind::star).binary_power() == 6 && info(token_kind::plusequal).aug_op() == 1 &&
              info(token_kind::kw_type).name_like() && !info(token_kind::rpar).starts_expression() && info(token_kind::kw_is).compare_op() == 0);

/// How a token kind reads in an error message: its text for a keyword or an operator (")",
/// "while"; the message quotes it), else a phrase ("a name", "end of file").
[[nodiscard]] constexpr std::string_view spelling(token_kind kind) noexcept {
    constexpr std::array<std::string_view, token_kind_count> names = [] {
        std::array<std::string_view, token_kind_count> n{};
        auto at = [&](token_kind k) -> std::string_view& { return n[static_cast<std::size_t>(k)]; };
        at(token_kind::end_of_file) = "end of file";
        at(token_kind::newline) = "a new line";
        at(token_kind::indent) = "an indent";
        at(token_kind::dedent) = "a dedent";
        at(token_kind::name) = "a name";
        at(token_kind::number) = "a number";
        at(token_kind::string) = "a string";
        at(token_kind::fstring_start) = "an f-string";
        at(token_kind::fstring_middle) = "f-string text";
        at(token_kind::fstring_end) = "the end of an f-string";
        for (std::size_t i = 0; i < detail::keyword_names.size(); ++i) n[static_cast<std::size_t>(token_kind::kw_False) + i] = detail::keyword_names[i];
        constexpr std::size_t first_op = static_cast<std::size_t>(token_kind::lpar);
        constexpr std::array<std::string_view, token_kind_count - first_op> ops{   // lpar .. exclamation, in their order
            "(", ")", "[", "]", "{", "}", ":", ",", ";", "+", "-", "*", "/", "|", "&", "<", ">", "=", ".", "%",
            "==", "!=", "<=", ">=", "~", "^", "<<", ">>", "**", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
            "<<=", ">>=", "**=", "//", "//=", "@", "@=", "->", "...", ":=", "!"};
        for (std::size_t i = 0; i < ops.size(); ++i) n[first_op + i] = ops[i];
        return n;
    }();
    return names[static_cast<std::size_t>(kind)];
}
static_assert(spelling(token_kind::rpar) == ")" && spelling(token_kind::exclamation) == "!" && spelling(token_kind::kw_while) == "while");

}  // namespace pygim::code::python
