#pragma once
// similarity/cpp_source.h — C++ source text to similarity units, without a
// compiler.
//
// CORE layer: pybind-free and constexpr, so the frontend's laws are proven
// at compile time beside the core's (tests/static/similarity_proofs.cpp).
// adapter/languages/cpp.h is the one-line descriptor that registers it.
//
// No C++ parser is small, and a similarity measure does not need one: it
// needs the same text to give the same tree, and close text close trees.
// This is a tokenizer plus a bracket-structure reader:
//
//   1. Tokens. Line splices (`\` before a line end, CRLF ones too) join lines
//      first, inside strings and comments as anywhere; comments and
//      preprocessor lines are dropped, and of every `#if` / `#elif` /
//      `#elifdef` / `#else` chain one branch is read: the first, or the next
//      after an `#if 0` (detail::conditionals), so branches that each open a
//      brace do not unbalance the text; identifiers, keywords (`final`,
//      `override`, `import` and `module` are names), numbers, string/char
//      literals (prefixes and raw strings included) and punctuators, longest
//      match first.
//   2. Units. A declaration scope (the file, a namespace, a class body) is
//      read declaration by declaration. A declaration whose name — the
//      identifier before its first top-level `(`, or an `operator` spelling —
//      is followed, after qualifiers, a trailing return or a constructor's
//      initialiser list, by `{` is a FUNCTION; its body is the brace group.
//      `class`/`struct`/`union` with a body is a CLASS, named by the last name
//      of its head (attributes, `alignas(...)` and macros before it skipped:
//      `class LLVM_ABI Foo final : Base`): its in-body function definitions
//      are its methods, everything else in the body (data members, member
//      declarations, nested types' text) is its header. `namespace X {`,
//      `inline namespace X {` and `extern "C" {` are scopes, `template <...>`
//      a prefix.
//      Qualified names carry their scope: `ns::Widget::draw`.
//   3. Trees. A body is a tree of bracket groups — `()`, `[]`, `{}` — and,
//      inside braces, statements split at `;` and after a nested `{}`.
//      Leaves are tokens: a keyword or punctuator is its own kind, a literal
//      is "lit" labelled with its type (int, float, string, char), an
//      identifier is "id" with a role:
//        attr  after `.`, `->` or `::`, or before `::` (a member, a scope)
//        bind  a declared name: after a type (an identifier, a type
//              keyword, a template's closing `>` or `>>`, or a `*`, `&` or
//              `&&` after one) and before `= ; , { ( [ : )` — a parameter, a
//              local. Only where declarations are read (statements,
//              parameter lists, `for` and `catch` heads, lambdas): in a
//              condition or a call's arguments `n > limit`, `n & mask` and
//              `not done` use their names. Plain assignment binds nothing:
//              `m_table = table` and `m_where = clause` assign different
//              members, and differ.
//        use   everything else
//      The core renames bound names; a declaration's TYPE is an identifier
//      used, so changing a parameter's name changes nothing and changing its
//      type does.
//   4. Names (names.h). `namespace fs = std::filesystem;` and `using
//      std::move;` anywhere in the file bind fs and move. A qualified name
//      — `std::move`, `fs::path`, `::std::move` — is one "qname" leaf
//      labelled with its fully qualified spelling (a leading alias
//      expanded, a leading `::` dropped), and an unqualified name a
//      using-declaration brought in is the same leaf, unless the unit binds
//      that name itself. So `std::move(x)`, `::std::move(x)` and `using
//      std::move; move(x)` are one tree. The binding statements themselves
//      are dropped from function bodies. `using namespace` binds nothing:
//      without a symbol table, which names it brings in is unknowable.
//
// It is a heuristic reader, deterministic by construction: the same bytes
// always give the same units and trees. What it gets wrong it gets wrong the
// same way every time (a macro that expands to a declarator, `a * b;` read as
// a declaration). Brackets still unbalanced after choosing branches are a
// parse_error naming the file and the line.
//
// A worked example:
//
//     namespace geo {
//     struct Point {
//         int x = 0;
//         int norm1() const { return abs(x) + abs(y); }
//     };
//     int twice(int v) { return v * 2; }
//     int doubled(int w) { return w * 2; }
//     }
//
//   parse_cpp -> geo::Point            class, line 2, methods {geo::Point::norm1}
//                geo::Point::norm1     function, line 4
//                geo::twice            function, line 6
//                geo::doubled          function, line 7   (exact match of twice)
//
// Cost: linear in the text for tokens and structure; trees are built once.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../utils/chars.h"
#include "../wiring/registry/core.h"
#include "core.h"
#include "names.h"

namespace pygim::similarity::cpp {

enum class tok : std::uint8_t { ident, keyword, number, string, character, punct };

namespace detail {

// The words a reader tells apart — keywords, literal prefixes, punctuators — as compile-time
// registries over the bucketed engine: a word is compared only with those of its first byte and
// its length (at most five among C++'s keywords: `c` and 8 bytes), where a linear search compared
// all 96. A word listed twice is a build error.
using word_table = core::StaticWordRegistry<bool, 128>;

template <std::size_t N>
[[nodiscard]] consteval word_table words(const std::array<std::string_view, N>& list) {
    word_table t;
    for (const std::string_view w : list) t.register_or_override(w, true, false);
    return t;
}
static_assert([] {
    const word_table t = words(std::to_array<std::string_view>({"do", "if"}));
    return t.contains("if") && !t.contains("in");   // 'i' and 2 bytes: compared, not equal
}());

// The keywords. `final`, `override`, `import` and `module` are not among them: each is a keyword
// only in its own place and a name everywhere else — `PyObject *module` is a parameter in 121 of
// CPython's C files, and a keyword there was never renamed (2026-10-29).
inline constexpr auto keyword_list = std::to_array<std::string_view>({
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
    "case", "catch", "char", "char16_t", "char32_t", "char8_t", "class", "co_await", "co_return",
    "co_yield", "compl", "concept", "const", "const_cast", "consteval", "constexpr", "constinit",
    "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if",
    "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not",
    "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
    "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
    "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
    "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
});
static_assert(keyword_list.size() == 92 && words(keyword_list).contains("co_await") && words(keyword_list).contains("static_assert") &&
              !words(keyword_list).contains("module") && !words(keyword_list).contains("i") && !words(keyword_list).contains(""));

// Keywords after which an identifier is NOT being declared: `return x;`,
// `case X:`, `new T(...)`, `goto L;`.
inline constexpr word_table expression_keywords = words(std::to_array<std::string_view>({
    "return", "case", "throw", "else", "new", "delete", "sizeof", "co_return", "co_yield",
    "co_await", "goto", "do", "typeid", "alignof",
    "and", "or", "not", "xor", "bitand", "bitor", "compl", "and_eq", "or_eq", "xor_eq", "not_eq",   // `not done` is an expression
}));
static_assert(expression_keywords.contains("return") && expression_keywords.contains("not") && !expression_keywords.contains("int"));

// The prefixes a string or character literal may carry: `u8"..."`, `LR"(...)"`.
inline constexpr word_table string_prefixes = words(std::to_array<std::string_view>({"u8", "u", "U", "L", "R", "u8R", "uR", "UR", "LR"}));
static_assert(string_prefixes.contains("u8R") && !string_prefixes.contains("u8x"));

// The punctuators longer than one character (three at most).
inline constexpr auto punctuator_list = std::to_array<std::string_view>({
    "<=>", ">>=", "<<=", "->*", "...", "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==",
    "!=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*", "##",
});
inline constexpr word_table punctuators = words(punctuator_list);

// Every keyword and punctuator — one character or more — numbered from 1 in that order: the word a
// syntax token is, in one byte (0: none, as for an identifier or a literal). The reader asks
// "is this `{`?" millions of times a file; a byte answers where a text compare did (#284).
inline constexpr auto single_punctuators = std::to_array<std::string_view>({
    "{", "}", "(", ")", "[", "]", ";", ",", ".", ":", "?", "~", "!", "+", "-", "*", "/", "%", "^",
    "&", "|", "=", "<", ">", "#", "@", "$", "`", "\\",
});
using word_ids = core::StaticWordRegistry<std::uint8_t, 192>;
inline constexpr word_ids syntax_words = [] {
    word_ids t;
    std::uint8_t id = 0;
    for (const std::string_view w : keyword_list) t.register_or_override(w, ++id, false);
    for (const std::string_view w : punctuator_list) t.register_or_override(w, ++id, false);
    for (const std::string_view w : single_punctuators) t.register_or_override(w, ++id, false);
    return t;
}();
static_assert(syntax_words.size() == 92 + 27 + 29 && syntax_words.value_or("alignas", 0) == 1 && syntax_words.value_or("\\", 0) == 148 &&
              syntax_words.value_or("x", 0) == 0);

/// The id of a keyword's or punctuator's text, 0 for any other.
[[nodiscard]] constexpr std::uint8_t syntax_id(std::string_view text) noexcept { return syntax_words.value_or(text, 0); }
static_assert(syntax_id("alignas") == 1 && syntax_id("{") != 0 && syntax_id("module") == 0);   // a contextual word is an identifier

// The longest punctuator each byte starts, read off the table (0: none longer than the byte), so
// a lookup tries only lengths that exist: `(` and `;` none, `=` two (`==`), `<` three, then two.
inline constexpr auto longest_from = [] {
    std::array<std::uint8_t, 256> out{};
    for (const auto& [p, _] : punctuators.storage().items()) {
        std::uint8_t& n = out[static_cast<unsigned char>(p[0])];
        n = std::max(n, static_cast<std::uint8_t>(p.size()));
    }
    return out;
}();
static_assert(longest_from['<'] == 3 && longest_from['='] == 2 && longest_from['('] == 0);

/// The length of the longest punctuator `s` starts with (`s` not empty): 1 when none longer does.
[[nodiscard]] constexpr std::size_t punctuator_length(std::string_view s) noexcept {
    for (std::size_t n = std::min<std::size_t>(longest_from[static_cast<unsigned char>(s[0])], s.size()); n >= 2; --n) {
        if (punctuators.contains(s.substr(0, n))) return n;
    }
    return 1;
}
static_assert(punctuator_length("<=>x") == 3 && punctuator_length("<<y") == 2 && punctuator_length("<y") == 1 &&
              punctuator_length("->*p") == 3 && punctuator_length("..") == 1 && punctuator_length(";") == 1);   // tokenize, at each punctuator
static_assert([] {   // every listed punctuator is matched whole: none longer than three, none shadowed
    for (const auto& [p, _] : punctuators.storage().items()) {
        if (p.size() > 3 || punctuator_length(p) != p.size()) return false;
    }
    return true;
}());

}  // namespace detail

/// One token: its kind, its text (a view into the source), the line it starts on, and for a keyword
/// or punctuator its word (detail::syntax_words; 0 for any other token). 24 bytes, the text first:
/// C++ runs about one token per 4.7 bytes, so a megabyte of source is 5 MB of them.
class token {
public:
    constexpr token(tok kind, std::string_view text, std::uint32_t line) noexcept
        : token(kind, text, line, kind == tok::punct || kind == tok::keyword ? detail::syntax_id(text) : std::uint8_t{0}) {}
    constexpr token(tok kind, std::string_view text, std::uint32_t line, std::uint8_t word) noexcept
        : m_text(text), m_line(line), m_kind(kind), m_word(word) {}

    [[nodiscard]] constexpr tok kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return m_text; }
    [[nodiscard]] constexpr std::uint32_t line() const noexcept { return m_line; }
    [[nodiscard]] constexpr std::uint8_t word() const noexcept { return m_word; }

private:
    std::string_view m_text;
    std::uint32_t m_line;
    tok m_kind;
    std::uint8_t m_word;
};
static_assert(token(tok::punct, "+=", 3).text() == "+=" && token(tok::punct, "+=", 3).line() == 3 && token(tok::punct, "+=", 3).word() != 0 &&
              token(tok::ident, "x", 1).word() == 0 && sizeof(token) == 24);   // tokenize's output

namespace detail {

/// One branch of every preprocessor conditional, chosen without
/// evaluating anything: an `#if` / `#ifdef` / `#ifndef` takes its first
/// branch and every `#elif` / `#else` of it is skipped — except that `#if 0`
/// (and `#elif 0`) is dead code by convention, so the next branch is taken
/// instead. Reading every branch would duplicate code and, wherever the
/// branches each open a brace, unbalance the text.
///
///     #if SIZEOF_VOID_P == 8   ->  taken
///     #elif ...                ->  skipped
///     #else                    ->  skipped
///     #if 0  ...  #else        ->  the #else is taken
class conditionals {
public:
    [[nodiscard]] constexpr bool active() const noexcept { return m_open.empty() || m_open.back().active(); }

    /// Applies one directive line (`#` included).
    constexpr void directive(std::string_view d) {
        std::size_t k = 1;
        while (k < d.size() && chars::is_blank(d[k])) ++k;
        std::size_t e = k;
        while (e < d.size() && chars::continues_identifier(d[e])) ++e;
        std::string_view rest = d.substr(e);
        for (std::string_view cut : {std::string_view("//"), std::string_view("/*")}) {
            const std::size_t at = rest.find(cut);
            if (at != std::string_view::npos) rest = rest.substr(0, at);
        }
        while (!rest.empty() && chars::is_blank(rest.front())) rest.remove_prefix(1);
        while (!rest.empty() && chars::is_line_space(rest.back())) rest.remove_suffix(1);
        const bool zero = rest == "0";
        switch (directives.value_or(d.substr(k, e - k), step::none)) {
            case step::opens_unless_zero: m_open.emplace_back(active(), !zero); break;
            case step::opens: m_open.emplace_back(active(), true); break;
            case step::next_unless_zero:
                if (!m_open.empty()) m_open.back().next_branch(!zero);
                break;
            case step::next:
                if (!m_open.empty()) m_open.back().next_branch(true);
                break;
            case step::closes:
                if (!m_open.empty()) m_open.pop_back();
                break;
            case step::none: break;   // #define, #include, #pragma, ...: no branch
        }
    }

private:
    // What a directive does to the open conditionals: opens one (taking its first branch, or with
    // `#if 0` not), moves to its next branch (`#elif 0` not taken), or closes it.
    enum class step : std::uint8_t { none, opens, opens_unless_zero, next, next_unless_zero, closes };
    static constexpr auto directives = [] {
        core::StaticWordRegistry<step, 16> t;
        for (const std::string_view w : {"ifdef", "ifndef"}) t.register_or_override(w, step::opens, false);
        t.register_or_override("if", step::opens_unless_zero, false);
        for (const std::string_view w : {"else", "elifdef", "elifndef"}) t.register_or_override(w, step::next, false);   // C23, C++23
        t.register_or_override("elif", step::next_unless_zero, false);
        t.register_or_override("endif", step::closes, false);
        return t;
    }();

    // One open conditional: whether its enclosing branch is taken, whether one of its own
    // branches was taken already, and whether the current one is.
    class chain {
    public:
        constexpr chain(bool parent, bool take) noexcept : m_parent(parent), m_taken(take), m_active(parent && take) {}
        [[nodiscard]] constexpr bool active() const noexcept { return m_active; }
        /// An `#elif` / `#else`: taken if it may be (`take`) and no earlier branch was.
        constexpr void next_branch(bool take) noexcept {
            const bool now = !m_taken && take;
            m_active = m_parent && now;
            m_taken = m_taken || now;
        }

    private:
        bool m_parent;
        bool m_taken;
        bool m_active;
    };
    std::vector<chain> m_open;
};
static_assert([] {   // the example above
    conditionals c;
    c.directive("#if SIZEOF_VOID_P == 8");
    const bool first = c.active();
    c.directive("#elif X");
    const bool elif = c.active();
    c.directive("#else");
    const bool otherwise = c.active();
    c.directive("#endif");
    c.directive("#if 0");
    const bool zero = c.active();
    c.directive("#else");
    const bool after_zero = c.active();
    c.directive("#endif");
    return first && !elif && !otherwise && !zero && after_zero && c.active();
}());

}  // namespace detail

/// The tokens of `src`, comments and preprocessor lines dropped; of every
/// preprocessor conditional only one branch (detail::conditionals). An
/// unterminated comment, string or character literal is a parse_error.
///
///     tokenize("x += 1; // c")   -> ident x, punct +=, number 1, punct ;
///
/// Cost: one pass; the tokens view `src`, which must outlive them.
[[nodiscard]] constexpr std::vector<token> tokenize(std::string_view src, std::string_view origin = "<string>") {
    using namespace detail;
    std::vector<token> out;
    out.reserve(src.size() / 4);   // C++ runs about one token per 4.7 bytes (LLVM): one allocation, rarely two
    std::uint32_t line = 1;
    bool line_start = true;   // only whitespace so far on this line
    detail::conditionals branches;
    std::size_t i = 0;
    const std::size_t n = src.size();
    auto at = [&](std::size_t k) -> char { return k < n ? src[k] : '\0'; };
    auto count_lines = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to; ++k) line += src[k] == '\n';
    };
    // The length of a line splice at k — a `\` before \n, or before \r\n in a CRLF checkout — or
    // 0. Splices join lines before anything else is read (translation phase 2): inside a string,
    // a comment or a directive as anywhere.
    auto splice = [&](std::size_t k) -> std::size_t {
        if (at(k) != '\\') return 0;
        if (at(k + 1) == '\n') return 2;
        return at(k + 1) == '\r' && at(k + 2) == '\n' ? 3 : 0;
    };
    // Past a line comment at i, its splices followed: `// a note \` goes on to the next line.
    auto skip_line_comment = [&] {
        while (i < n && src[i] != '\n') {
            if (const std::size_t s = splice(i)) {
                ++line;
                i += s;
            } else {
                ++i;
            }
        }
    };
    while (i < n) {
        const char c = src[i];
        if (c == '\n') {
            ++line;
            line_start = true;
            ++i;
            continue;
        }
        if (chars::is_line_space(c)) {   // a run of them: indentation, alignment
            do ++i;
            while (i < n && chars::is_blank(src[i]));
            continue;
        }
        if (const std::size_t s = splice(i)) {   // a line splice
            ++line;
            i += s;
            continue;
        }
        if (c == '/' && at(i + 1) == '/') {
            skip_line_comment();
            continue;
        }
        if (c == '/' && at(i + 1) == '*') {
            const std::size_t end = src.find("*/", i + 2);
            if (end == std::string_view::npos) throw make_parse_error(origin, line, "unterminated /* comment");
            count_lines(i, end);
            i = end + 2;
            continue;
        }
        if (c == '#' && line_start) {   // a directive, continued lines included
            const std::size_t from = i;
            while (i < n && src[i] != '\n') {
                if (src[i] == '/' && at(i + 1) == '/') {   // a line comment: the rest of the line, its splices followed
                    skip_line_comment();
                    break;
                }
                if (src[i] == '"' || src[i] == '\'') {   // a quoted header name, message or character:
                    const char q = src[i];                // `#include "engines/*.h"` opens no comment, '"' no string
                    const std::size_t open = i++;
                    while (i < n && src[i] != q && src[i] != '\n') i += splice(i) ? splice(i) : src[i] == '\\' ? 2 : 1;
                    count_lines(open, std::min(i, n));   // the line splices it crossed
                    if (i < n && src[i] == q) ++i;
                    continue;
                }
                if (src[i] == '/' && at(i + 1) == '*') {   // a comment continues the directive across lines
                    const std::size_t end = src.find("*/", i + 2);
                    if (end == std::string_view::npos) throw make_parse_error(origin, line, "unterminated /* comment");
                    count_lines(i, end);
                    i = end + 2;
                    continue;
                }
                if (const std::size_t s = splice(i)) {
                    ++line;
                    i += s;
                    continue;
                }
                ++i;
            }
            branches.directive(src.substr(from, i - from));
            continue;
        }
        if (!branches.active()) {   // inside a branch not taken: skip to the next directive
            line_start = false;     // whitespace went on above
            ++i;
            continue;
        }
        line_start = false;
        const std::uint32_t start_line = line;
        const std::size_t start = i;

        // Identifiers, keywords and literal prefixes.
        if (chars::starts_identifier(c)) {
            while (i < n && chars::continues_identifier(src[i])) ++i;
            const std::string_view word = src.substr(start, i - start);
            if (string_prefixes.contains(word) && (at(i) == '"' || (at(i) == '\'' && word.back() != 'R'))) {
                // fall through to the literal readers below with the prefix included
            } else {
                const std::uint8_t id = syntax_id(word);   // a keyword's word, or 0: an identifier
                out.push_back(token{id != 0 ? tok::keyword : tok::ident, word, start_line, id});
                continue;
            }
        }
        // Raw strings: R"delim( ... )delim".
        if (at(i) == '"' && i > start && src[i - 1] == 'R') {
            const std::size_t open = src.find('(', i + 1);
            if (open == std::string_view::npos) throw make_parse_error(origin, start_line, "malformed raw string literal");
            std::string close = ")";
            close += src.substr(i + 1, open - i - 1);
            close += '"';
            const std::size_t end = src.find(close, open + 1);
            if (end == std::string_view::npos) throw make_parse_error(origin, start_line, "unterminated raw string literal");
            count_lines(i, end);
            i = end + close.size();
            while (i < n && chars::continues_identifier(src[i])) ++i;   // a user-defined suffix
            out.push_back(token{tok::string, src.substr(start, i - start), start_line});
            continue;
        }
        // Quoted literals.
        if (at(i) == '"' || at(i) == '\'') {
            const char q = src[i];
            ++i;
            while (i < n && src[i] != q) {
                if (src[i] == '\n') throw make_parse_error(origin, start_line, "unterminated literal");
                if (const std::size_t s = splice(i)) {   // \r\n too: a backslash then \r was read as an escape
                    i += s;
                    continue;
                }
                if (src[i] == '\\') ++i;
                ++i;
            }
            if (i >= n) throw make_parse_error(origin, start_line, "unterminated literal");
            count_lines(start, i);   // only line splices: a bare newline was refused above
            ++i;
            while (i < n && chars::continues_identifier(src[i])) ++i;   // a user-defined suffix
            out.push_back(token{q == '"' ? tok::string : tok::character, src.substr(start, i - start), start_line});
            continue;
        }
        // Numbers: the pp-number grammar (digit separators and exponent signs included).
        if (chars::is_digit(c) || (c == '.' && chars::is_digit(at(i + 1)))) {
            ++i;
            while (i < n) {
                const char d = src[i];
                if ((d == '+' || d == '-') &&
                    (src[i - 1] == 'e' || src[i - 1] == 'E' || src[i - 1] == 'p' || src[i - 1] == 'P')) {
                    ++i;
                } else if (chars::continues_identifier(d) || d == '.' || (d == '\'' && chars::continues_identifier(at(i + 1)))) {
                    ++i;
                } else {
                    break;
                }
            }
            out.push_back(token{tok::number, src.substr(start, i - start), start_line});
            continue;
        }
        // Punctuators, longest match first.
        const std::size_t len = punctuator_length(src.substr(i));
        out.push_back(token{tok::punct, src.substr(i, len), start_line, syntax_id(src.substr(i, len))});
        i += len;
    }
    return out;
}
static_assert([] {   // the example above
    const std::vector<token> t = tokenize("x += 1; // c");
    return t.size() == 4 && t[0].kind() == tok::ident && t[1].text() == "+=" && t[2].kind() == tok::number && t[3].text() == ";";
}());

namespace detail {

/// A keyword or punctuator where the reader asks about a token, named by its text: made at compile
/// time (a text that is no syntax word does not build) and compared as one byte.
class word {
public:
    template <std::size_t N>
    consteval word(const char (&text)[N]) : m_id(syntax_id(std::string_view(text, N - 1))) {   // NOLINT: a literal names it
        if (m_id == 0) throw "not a C++ keyword or punctuator";
    }
    [[nodiscard]] constexpr std::uint8_t id() const noexcept { return m_id; }

private:
    std::uint8_t m_id;
};
static_assert(word("alignas").id() == 1 && word("{").id() == syntax_id("{"));

[[nodiscard]] constexpr bool is(const token& t, word w) noexcept { return t.word() == w.id(); }
static_assert(is(token{tok::punct, "::", 1}, "::") && !is(token{tok::ident, "x", 1}, "{") && is(token{tok::keyword, "namespace", 1}, "namespace"));

// Each word's atom, made once: a syntax leaf's kind (the same atoms as of its text).
inline constexpr auto word_atoms = [] {
    std::array<atom, 256> out{};
    for (const auto& [w, id] : syntax_words.storage().items()) out[id] = atom(w);
    return out;
}();
static_assert(word_atoms[syntax_id("{")] == atom("{") && word_atoms[syntax_id("return")] == atom("return"));

// The keywords after which an identifier is not being declared, by word.
inline constexpr auto expression_words = [] {
    std::array<bool, 256> out{};
    for (const auto& [w, _] : expression_keywords.storage().items()) out[syntax_id(w)] = true;
    return out;
}();
static_assert(expression_words[syntax_id("return")] && !expression_words[syntax_id("int")]);

// The kinds the reader emits on every token, made atoms once.
inline constexpr atom id_kind{"id"};
inline constexpr atom lit_kind{"lit"};
inline constexpr atom qname_kind{"qname"};
inline constexpr atom parens_kind{"()"};
inline constexpr atom brackets_kind{"[]"};
inline constexpr atom braces_kind{"{}"};
inline constexpr atom statement_kind{"stmt"};

/// Whether a number's text is a floating literal: a `.` or an exponent — `e`, or `p` after `0x`,
/// where an `e` is a digit.
[[nodiscard]] constexpr bool floating(std::string_view number) noexcept {
    const bool hex = number.size() > 1 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X');
    return number.find_first_of(hex ? "pP" : ".eE") != std::string_view::npos;
}
static_assert(floating("0x1p3") && !floating("0xE") && floating("1.5") && !floating("15"));   // a hex E is a digit, a p an exponent

// The literal's type, what the core compares instead of its value, by token kind (a number's by
// floating()): atoms made once.
inline constexpr atom float_type{"float"}, int_type{"int"};
inline constexpr auto literal_types = [] {
    std::array<atom, 6> out{};   // by tok; only the literal kinds are read
    out[static_cast<std::size_t>(tok::string)] = atom("string");
    out[static_cast<std::size_t>(tok::character)] = atom("char");
    return out;
}();
[[nodiscard]] constexpr atom literal_atom(const token& t) noexcept {
    if (t.kind() == tok::number) return floating(t.text()) ? float_type : int_type;
    return literal_types[static_cast<std::size_t>(t.kind())];
}
static_assert(literal_atom(token{tok::number, "1.5", 1}) == atom("float") && literal_atom(token{tok::string, "\"a\"", 1}) == atom("string") &&
              literal_atom(token{tok::character, "'a'", 1}) == atom("char"));

class reader {
public:
    constexpr reader(std::vector<token> toks, std::string_view origin) : m_tokens(std::move(toks)), m_origin(origin) {
        if (m_tokens.size() >= no_mate) throw make_parse_error(m_origin, 1, "more than 2^32 - 2 tokens");
        // The matching bracket of every bracket token.
        m_mate.assign(m_tokens.size(), no_mate);
        std::vector<std::uint32_t> stack;
        for (std::uint32_t i = 0; i < m_tokens.size(); ++i) {
            const token& x = m_tokens[i];
            if (is(x, "(") || is(x, "[") || is(x, "{")) {
                stack.push_back(i);
            } else if (const char want = is(x, ")") ? '(' : is(x, "]") ? '[' : is(x, "}") ? '{' : '\0'; want != '\0') {
                if (stack.empty() || m_tokens[stack.back()].text()[0] != want) {
                    std::string what = "unmatched '";
                    what += x.text();
                    what += "'";
                    throw make_parse_error(m_origin, x.line(), what);
                }
                m_mate[stack.back()] = i;
                m_mate[i] = stack.back();
                stack.pop_back();
            }
        }
        if (!stack.empty()) {
            std::string what = "unclosed '";
            what += m_tokens[stack.back()].text();
            what += "'";
            throw make_parse_error(m_origin, m_tokens[stack.back()].line(), what);
        }
        collect_aliases();
    }

    constexpr std::vector<parsed_unit> run() {
        scope(0, m_tokens.size(), {}, npos, nullptr);
        return std::move(m_units);
    }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    static constexpr std::uint32_t no_mate = std::numeric_limits<std::uint32_t>::max();   // a token that is no bracket
    std::vector<token> m_tokens;
    std::string_view m_origin;
    std::vector<std::uint32_t> m_mate;   // each bracket's matching one: half the bytes of a size_t a token
    std::vector<parsed_unit> m_units;
    binding_table m_aliases;                       // namespace aliases and using-declarations, file-wide
    mapping::flat_storage<atom, atom> m_using_names;   // a name they bind -> what it resolves to: a tree's labels are atoms

    // `namespace fs = std::filesystem;` binds fs, `using std::move;` binds
    // move — anywhere in the file (a file-wide over-approximation: a name
    // bound two ways is ambiguous and stays as written). `using namespace`
    // binds nothing: without a symbol table it cannot say which names it
    // brings in.
    constexpr void collect_aliases() {
        std::vector<std::string> bound;
        binding_table::builder aliases;
        auto bind = [&](std::string_view local, std::string_view qualified) {
            bound.emplace_back(local);
            aliases.bind(local, qualified);
        };
        for (std::size_t i = 0; i + 1 < m_tokens.size(); ++i) {
            if (at(i, "namespace") && m_tokens[i + 1].kind() == tok::ident && at(i + 2, "=")) {
                const auto [end, name] = qualified_name_at(i + 3, m_tokens.size(), false);
                if (end != i + 3 && at(end, ";")) bind(m_tokens[i + 1].text(), name);
            } else if (at(i, "using") && !at(i + 1, "namespace")) {
                const auto [end, name] = qualified_name_at(i + 1, m_tokens.size(), false);
                if (end != i + 1 && at(end, ";") && name.find("::") != std::string::npos)
                    bind(std::string_view(name).substr(name.rfind("::") + 2), name);
            }
        }
        m_aliases = std::move(aliases).build();
        std::vector<std::pair<atom, atom>> renames;
        for (const std::string& n : bound) {
            if (const auto q = m_aliases.resolve(n)) renames.emplace_back(atom(n), atom(*q));
        }
        m_using_names.assign_bulk(std::move(renames));   // a name bound twice to one target: once
    }

    // The qualified-name run starting at i (`a::b::c`, `::std::move`) and
    // the index after it; (i, "") when none starts there. With `resolve`, a
    // leading alias is expanded (`fs::path` -> "std::filesystem::path"); a
    // leading global `::` is dropped either way.
    [[nodiscard]] constexpr std::pair<std::size_t, std::string> qualified_name_at(std::size_t i, std::size_t hi, bool resolve) const {
        std::size_t j = i;
        if (at(j, "::")) ++j;
        if (j >= hi || m_tokens[j].kind() != tok::ident) return {i, std::string()};
        std::string name(m_tokens[j].text());
        if (resolve) {
            if (const auto q = m_aliases.resolve(name)) name = std::string(*q);
        }
        ++j;
        while (j + 1 < hi && at(j, "::") && m_tokens[j + 1].kind() == tok::ident) {
            name += "::";
            name += m_tokens[j + 1].text();
            j += 2;
        }
        return {j, name};
    }

    // Whether a qualified-name run starts at i within [lo, hi): an identifier
    // followed by `::name`, or a global `::name` not continuing a run.
    [[nodiscard]] constexpr bool qualified_name_starts(std::size_t i, std::size_t lo, std::size_t hi) const {
        if (m_tokens[i].kind() == tok::ident)
            return i + 2 < hi && at(i + 1, "::") && m_tokens[i + 2].kind() == tok::ident &&
                   !(i > lo && (at(i - 1, ".") || at(i - 1, "->")));
        if (at(i, "::") && i + 1 < hi && m_tokens[i + 1].kind() == tok::ident)
            return !(i > lo && (m_tokens[i - 1].kind() == tok::ident || at(i - 1, ">")));
        return false;
    }

    // A unit's unqualified names brought in by a using-declaration, resolved
    // — unless the unit binds the name itself (a local `move` is not std::move).
    constexpr void resolve_using(tree& tr) const {
        if (m_using_names.empty()) return;
        std::vector<atom> bound;
        for (const node& n : tr.nodes()) {
            if (n.role() == role::bind) bound.push_back(n.label());
        }
        std::sort(bound.begin(), bound.end());
        for (std::size_t i = 0; i < tr.nodes().size(); ++i) {
            const node& n = tr.nodes()[i];
            if (n.kind() != id_kind || n.role() != role::use || std::binary_search(bound.begin(), bound.end(), n.label())) continue;
            if (const atom* q = m_using_names.find(n.label())) tr.relabel(i, qname_kind, *q, role::attr);
        }
    }

    // A statement that only binds names (`using std::move;`, `using
    // namespace std;`, `namespace fs = ...;`): dropped from trees, like a
    // Python import — what it binds is already in the names.
    [[nodiscard]] constexpr bool binding_only(std::size_t lo, std::size_t hi) const {
        if (at(lo, "namespace")) return true;
        if (!at(lo, "using")) return false;
        if (at(lo + 1, "namespace")) return true;
        for (std::size_t i = lo; i < hi; ++i) {
            if (at(i, "=")) return false;   // `using T = ...;` is a type alias: kept
        }
        return true;
    }

    [[nodiscard]] constexpr bool open_bracket(std::size_t i) const noexcept { return is(m_tokens[i], "(") || is(m_tokens[i], "[") || is(m_tokens[i], "{"); }
    [[nodiscard]] constexpr bool at(std::size_t i, word w) const noexcept { return i < m_tokens.size() && is(m_tokens[i], w); }

    // After a balanced `<...>` starting at i (a template parameter list):
    // nested brackets are skipped whole, `>>` closes two.
    [[nodiscard]] constexpr std::size_t skip_angles(std::size_t i, std::size_t end) const noexcept {
        int depth = 0;
        for (; i < end; ++i) {
            if (open_bracket(i)) {
                i = m_mate[i];
                continue;
            }
            if (at(i, "<")) ++depth;
            else if (at(i, ">")) --depth;
            else if (at(i, ">>")) depth -= 2;
            if (depth <= 0) return i + 1;
        }
        return end;
    }

    // The index after the `;` ending the declaration at i (brackets skipped whole).
    [[nodiscard]] constexpr std::size_t to_semicolon(std::size_t i, std::size_t end) const noexcept {
        for (; i < end; ++i) {
            if (open_bracket(i)) i = m_mate[i];
            else if (at(i, ";")) return i + 1;
        }
        return end;
    }

    static constexpr std::string join(const std::vector<std::string>& prefix, std::string_view leaf) {
        std::string out;
        for (const std::string& p : prefix) {
            out += p;
            out += "::";
        }
        out += leaf;
        return out;
    }

    // ── tree building ──

    // What a run of tokens may declare: a statement, a parameter list, a `for` or `catch` head and a
    // lambda's parameters declare names; a condition, a call's arguments, a subscript and an
    // initialiser list only use them.
    enum class reading : std::uint8_t { declarations, expressions };

    constexpr void leaf(tree_builder& b, std::size_t i, std::size_t lo, std::size_t hi, reading where) const {
        const token& x = m_tokens[i];
        if (x.kind() == tok::number || x.kind() == tok::string || x.kind() == tok::character) {
            b.leaf(lit_kind, literal_atom(x), role::constant);
            return;
        }
        if (x.kind() != tok::ident) {
            b.leaf(x.word() != 0 ? word_atoms[x.word()] : atom(x.text()));   // a byte not ASCII is part of an identifier
            return;
        }
        const token* prev = i > lo ? &m_tokens[i - 1] : nullptr;
        const token* next = i + 1 < hi ? &m_tokens[i + 1] : nullptr;
        if ((prev && (is(*prev, ".") || is(*prev, "->") || is(*prev, "::"))) || (next && is(*next, "::"))) {
            b.leaf(id_kind, atom(x.text()), role::attr);
            return;
        }
        const bool declarator_next = !next || is(*next, "=") || is(*next, ";") || is(*next, ",") ||
                                     is(*next, "{") || is(*next, "(") || is(*next, "[") || is(*next, ":") ||
                                     is(*next, ")");
        // Only a declaration binds: `m_table = table` assigns a member (or a
        // global) whose name must count; a local assigned later is still
        // renamed, through its declaration.
        const role declared = declarator_next && after_a_type(i, lo, where) ? role::bind : role::use;
        b.leaf(id_kind, atom(x.text()), declared);
    }

    // Whether the name at i follows a type, so names a declarator: a name (`T x`), a keyword that
    // names or qualifies a type (`int x`, `const x`; not `return x`, `not x`), a template's closing
    // `>` or `>>` (`vector<int> v`, `map<K, vector<V>> m`), or a `*`, `&` or `&&` after one (`T* p`,
    // `const T& r`). Found 2026-10-29: any `>`, `*`, `&` or `&&` was taken for a type's, so in
    // `if (n > limit)` or `g(n & mask)` the global was taken for a local and renamed away.
    [[nodiscard]] constexpr bool after_a_type(std::size_t i, std::size_t lo, reading where) const {
        if (i <= lo) return false;
        const std::size_t p = i - 1;
        if (m_tokens[p].kind() == tok::ident) return true;
        if (m_tokens[p].kind() == tok::keyword) return !expression_words[m_tokens[p].word()];
        if (is(m_tokens[p], ">") || is(m_tokens[p], ">>")) return closes_template(p, lo);
        if (is(m_tokens[p], "*") || is(m_tokens[p], "&") || is(m_tokens[p], "&&")) return p > lo && a_type_ends_at(p - 1, lo, where);
        return false;
    }
    // Whether a type ends at k, before a `*` or `&`: a type keyword, a template's close, another
    // `*` or `&` (`T** p`), or — where declarations are read — a name that starts its declaration
    // (first in the run, after a `,`, or after a qualifying keyword: `static T* p`). A name in an
    // expression before `&` is an operand: `n & mask`.
    [[nodiscard]] constexpr bool a_type_ends_at(std::size_t k, std::size_t lo, reading where) const {
        if (m_tokens[k].kind() == tok::keyword) return !expression_words[m_tokens[k].word()];
        if (is(m_tokens[k], ">") || is(m_tokens[k], ">>")) return closes_template(k, lo);
        if (is(m_tokens[k], "*") || is(m_tokens[k], "&")) return k > lo && a_type_ends_at(k - 1, lo, where);
        if (m_tokens[k].kind() != tok::ident || where == reading::expressions) return false;
        std::size_t first = k;   // the name's first token: `a::b::T`, `::T`
        while (first >= lo + 2 && is(m_tokens[first - 1], "::") && m_tokens[first - 2].kind() == tok::ident) first -= 2;
        if (first > lo && is(m_tokens[first - 1], "::")) --first;
        return first == lo || is(m_tokens[first - 1], ",") || (m_tokens[first - 1].kind() == tok::keyword && !expression_words[m_tokens[first - 1].word()]);
    }
    // Whether the `>` or `>>` at k closes template arguments opened in [lo, k): its `<` found
    // before any `;`, `=`, `{` or `}`, bracket groups passed whole. `n > limit` has none.
    [[nodiscard]] constexpr bool closes_template(std::size_t k, std::size_t lo) const {
        int open = is(m_tokens[k], ">>") ? 2 : 1;
        for (std::size_t q = k; q-- > lo;) {
            if (is(m_tokens[q], ")") || is(m_tokens[q], "]")) {
                q = m_mate[q];
                continue;
            }
            if (is(m_tokens[q], ">")) ++open;
            else if (is(m_tokens[q], ">>")) open += 2;
            else if (is(m_tokens[q], "<") && --open == 0) return true;
            else if (is(m_tokens[q], ";") || is(m_tokens[q], "=") || is(m_tokens[q], "{") || is(m_tokens[q], "}")) return false;
        }
        return false;
    }

    // Tokens [lo, hi) as children of the open node: bracket groups as nodes,
    // a qualified name (`std::move`, `fs::path`) as one "qname" leaf with its
    // leading alias expanded.
    constexpr void tokens(tree_builder& b, std::size_t lo, std::size_t hi, reading where) const {
        for (std::size_t i = lo; i < hi; ++i) {
            if (open_bracket(i)) {
                group(b, i);
                i = m_mate[i];
            } else if (qualified_name_starts(i, lo, hi)) {
                const auto [end, name] = qualified_name_at(i, hi, true);
                b.leaf(qname_kind, atom(name), role::attr);
                i = end - 1;
            } else {
                leaf(b, i, lo, hi, where);
            }
        }
    }

    // The group opened at i: `()` and `[]` hold tokens, `{}` holds statements.
    constexpr void group(tree_builder& b, std::size_t i) const {
        const std::size_t close = m_mate[i];
        if (!is(m_tokens[i], "{")) {
            b.open(is(m_tokens[i], "(") ? parens_kind : brackets_kind);
            tokens(b, i + 1, close, declares_inside(i) ? reading::declarations : reading::expressions);
            b.close();
            return;
        }
        b.open(braces_kind);
        statements(b, i + 1, close);
        b.close();
    }
    // Whether the `(` at i opens a head that declares: `for (`, `catch (`, or a lambda's parameters
    // — a `[` group's `(` where that `[` is no subscript (`v[k](x)` calls).
    [[nodiscard]] constexpr bool declares_inside(std::size_t i) const {
        if (!is(m_tokens[i], "(") || i == 0) return false;
        const token& before = m_tokens[i - 1];
        if (is(before, "for") || is(before, "catch")) return true;
        if (!is(before, "]")) return false;
        const std::size_t open = m_mate[i - 1];
        return open == 0 || !(m_tokens[open - 1].kind() == tok::ident || is(m_tokens[open - 1], ")") || is(m_tokens[open - 1], "]"));
    }

    // Statements of a brace group: split at `;` and after a nested `{}`.
    constexpr void statements(tree_builder& b, std::size_t lo, std::size_t hi) const {
        std::size_t start = lo;
        auto flush = [&](std::size_t end) {
            if (end > start && !binding_only(start, end)) {
                b.open(statement_kind);
                tokens(b, start, end, reading::declarations);
                b.close();
            }
        };
        for (std::size_t i = lo; i < hi; ++i) {
            if (at(i, ";")) {
                flush(i);
                start = i + 1;
            } else if (at(i, "{")) {
                i = m_mate[i];
                flush(i + 1);
                start = i + 1;
            } else if (open_bracket(i)) {
                i = m_mate[i];
            }
        }
        flush(hi);
    }

    // ── declaration scopes ──

    // Reads declarations in [i, end). `cls` is the enclosing class's unit
    // index (npos outside a class) and `header` its header tree, which
    // receives every member that is not a function or a nested class.
    constexpr void scope(std::size_t i, std::size_t end, std::vector<std::string> prefix, std::size_t cls,
                         tree_builder* header) {
        while (i < end) {
            if (at(i, ";")) {
                ++i;
                continue;
            }
            if ((at(i, "public") || at(i, "private") || at(i, "protected")) && at(i + 1, ":")) {
                i += 2;
                continue;
            }
            if (at(i, "template") && at(i + 1, "<")) {
                i = skip_angles(i + 1, end);
                continue;
            }
            if ((at(i, "inline") || at(i, "export")) && at(i + 1, "namespace")) {   // `inline namespace v1 {`: a scope like any
                ++i;
                continue;
            }
            if (at(i, "namespace")) {
                std::size_t j = i + 1;
                std::string name;
                while (j < end && (m_tokens[j].kind() == tok::ident || at(j, "::") || at(j, "inline"))) {
                    if (m_tokens[j].kind() == tok::ident || at(j, "::")) name += m_tokens[j].text();
                    ++j;
                }
                if (at(j, "{")) {
                    auto inner = prefix;
                    inner.push_back(name.empty() ? std::string("{anonymous}") : name);
                    scope(j + 1, m_mate[j], std::move(inner), npos, nullptr);
                    i = m_mate[j] + 1;
                } else {
                    i = to_semicolon(j, end);   // a namespace alias
                }
                continue;
            }
            if (at(i, "extern") && i + 2 < end && m_tokens[i + 1].kind() == tok::string && at(i + 2, "{")) {
                scope(i + 3, m_mate[i + 2], prefix, npos, nullptr);
                i = m_mate[i + 2] + 1;
                continue;
            }
            if ((at(i, "class") || at(i, "struct") || at(i, "union")) && class_definition(i, end, prefix, header, i)) {
                continue;
            }
            i = declaration(i, end, prefix, cls, header);
        }
    }

    // A class definition at i: true (and `next` set past it) when there is a body; false for
    // `class X;`, `struct X x;` or `struct X f(...)`. Its head, read to the body:
    //
    //     class [[nodiscard]] alignas(16) LLVM_ABI ns::Name final : public Base<T(int)> {
    //           ^ attributes, alignas(...) and macro calls before the name are its specifiers
    //                                     ^ the name: the last name before `:` or `{`, with its `::` chain
    //                                              ^ `final` after the name is no name
    //
    // A `(` group before the body is a specifier when a name, an attribute or another specifier
    // follows it, and else a declarator (`struct X f(int)`). Found 2026-10-29: every `(` before the
    // body refused the class, so `struct alignas(16) S {...}` lost S and its methods; and the first
    // name was taken, so `class LLVM_ABI Foo` was named LLVM_ABI.
    constexpr bool class_definition(std::size_t i, std::size_t end, const std::vector<std::string>& prefix,
                                    tree_builder* outer_header, std::size_t& next) {
        std::size_t body = npos, colon = npos, name_at = npos, name_last = npos;
        for (std::size_t j = i + 1; j < end; ++j) {
            if (at(j, "{")) {
                body = j;
                break;
            }
            if (at(j, ";") || at(j, "=")) return false;
            if (colon != npos) {   // the base clause: whatever it holds, to the body
                if (open_bracket(j)) j = m_mate[j];
                continue;
            }
            if (at(j, ":")) {
                colon = j;
            } else if (at(j, "[")) {
                j = m_mate[j];   // [[attributes]]
            } else if (at(j, "(")) {
                const std::size_t after = m_mate[j] + 1;
                const bool specifier = after < end && (m_tokens[after].kind() == tok::ident || at(after, "[") || at(after, "alignas"));
                if (!specifier) return false;
                if (name_last == j - 1) name_at = name_last = npos;   // the macro called was no name
                j = m_mate[j];
            } else if (at(j, "<") && name_at != npos) {
                j = skip_angles(j, end) - 1;   // a specialization's arguments: `class Foo<int> {`
            } else if (m_tokens[j].kind() == tok::ident && !(m_tokens[j].text() == "final" && name_at != npos)) {
                name_at = j;
                j = name_last = qualified_name_at(j, end, false).first - 1;   // `ns::Name`: its last token
            }
        }
        if (body == npos) return false;
        std::uint32_t line = m_tokens[i].line();
        std::string name;
        if (name_at != npos) {
            line = m_tokens[name_at].line();
            for (std::size_t k = name_at; k <= name_last; ++k) name += m_tokens[k].text();
        }
        if (name.empty()) name = "{anonymous}";
        const std::size_t close = m_mate[body];

        const std::size_t me = m_units.size();
        m_units.push_back(parsed_unit{unit_kind::class_, join(prefix, name), line, {}, {}});
        tree_builder h;
        h.open("class");
        h.leaf(m_tokens[i].text());
        // The base clause: from the head's `:` to the body.
        if (colon != npos) {
            h.open("bases");
            tokens(h, colon + 1, body, reading::expressions);
            h.close();
        }
        auto inner = prefix;
        inner.push_back(name);
        scope(body + 1, close, std::move(inner), me, &h);
        h.close();
        tree header = std::move(h).finish();
        resolve_using(header);
        m_units[me].set_body(std::move(header));

        // `} x;` declares a variable of the class too: part of the outer scope.
        next = to_semicolon(close + 1, end);
        if (outer_header && next > close + 2) {
            outer_header->open("member");
            tokens(*outer_header, close + 1, next - 1, reading::declarations);
            outer_header->close();
        }
        return true;
    }

    // One declaration at i; a function definition becomes a unit. Returns the
    // index after it.
    constexpr std::size_t declaration(std::size_t i, std::size_t end, const std::vector<std::string>& prefix,
                                      std::size_t cls, tree_builder* header) {
        std::size_t params = npos;     // the `(` of the parameter list
        std::size_t name_at = npos;    // the name's first token
        std::size_t name_end = npos;   // one past the name's last token
        std::size_t init = npos;       // a constructor's `:`
        std::size_t body = npos;
        std::size_t j = i;
        for (; j < end; ++j) {
            if (at(j, ";")) break;
            if (at(j, "operator") && params == npos) {
                name_at = j;
                std::size_t k = j + 1;
                if (at(k, "(") && at(k + 1, ")")) k += 2;   // operator()
                else if (at(k, "[") && at(k + 1, "]")) k += 2;
                while (k < end && !at(k, "(") && !at(k, ";") && !at(k, "{")) ++k;
                if (!at(k, "(")) break;
                name_end = k;
                params = k;
                j = m_mate[k];
                continue;
            }
            if (at(j, "(")) {
                if (init == npos && j > i && m_tokens[j - 1].kind() == tok::ident) {
                    params = j;
                    name_end = j;
                    name_at = j - 1;
                    if (name_at > i && at(name_at - 1, "~")) --name_at;
                }
                j = m_mate[j];
                continue;
            }
            if (at(j, "[")) {
                j = m_mate[j];
                continue;
            }
            if (at(j, "=") && params == npos) {   // a variable's initialiser
                j = to_semicolon(j, end) - 1;
                break;
            }
            if (at(j, ":") && params != npos && init == npos) {
                init = j;
                continue;
            }
            if (at(j, "{")) {
                // In an initialiser list, `member{...}` is an item, not the body.
                if (init != npos && j > init + 1 && (m_tokens[j - 1].kind() == tok::ident || at(j - 1, ">"))) {
                    j = m_mate[j];
                    continue;
                }
                if (params == npos) {   // brace initialisation, an enum, an aggregate
                    j = m_mate[j];
                    continue;
                }
                body = j;
                break;
            }
        }
        if (body == npos) {
            const std::size_t next = j < end ? j + 1 : end;
            if (header && j > i) {
                header->open("member");
                tokens(*header, i, j, reading::declarations);
                header->close();
            }
            return next;
        }

        // The qualified name: `A::B::name`, a destructor's `~`, an operator spelling.
        std::size_t q = name_at;
        while (q >= i + 2 && at(q - 1, "::") && m_tokens[q - 2].kind() == tok::ident) q -= 2;
        std::string name;
        for (std::size_t k = q; k < name_end; ++k) name += m_tokens[k].text();

        tree_builder b;
        b.open("function");
        b.open("params");
        tokens(b, params + 1, m_mate[params], reading::declarations);
        b.close();
        if (init != npos) {
            b.open("init");
            tokens(b, init + 1, body, reading::expressions);
            b.close();
        }
        group(b, body);
        b.close();

        const std::size_t me = m_units.size();
        tree built = std::move(b).finish();
        resolve_using(built);
        m_units.push_back(parsed_unit{unit_kind::function, join(prefix, name), m_tokens[name_at].line(), std::move(built), {}});
        if (cls != npos) m_units[cls].add_method(static_cast<std::uint32_t>(me));
        std::size_t next = m_mate[body] + 1;
        if (at(next, ";")) ++next;
        return next;
    }
};
static_assert(reader(tokenize("int f(int a) { return a; }"), "f.cpp").run().size() == 1);   // parse_cpp's one call

}  // namespace detail

/// Every function, method and class `src` defines, in source order (a class
/// before its methods). `origin` names the source in errors.
/// Cost: linear in the text, plus the trees.
[[nodiscard]] constexpr std::vector<parsed_unit> parse_cpp(std::string_view src, std::string_view origin = "<string>") {
    return detail::reader(tokenize(src, origin), origin).run();
}
static_assert([] {   // the file comment's worked example, in brief
    const std::vector<parsed_unit> u = parse_cpp("namespace geo {\nint twice(int v) { return v * 2; }\n}\n", "geo.h");
    return u.size() == 1 && u[0].qualname() == "geo::twice" && u[0].line() == 2;
}());

}  // namespace pygim::similarity::cpp
