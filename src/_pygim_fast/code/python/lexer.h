#pragma once
// code/python/lexer.h — Python 3.14 source to tokens, one at a time, as the parser asks.
//
//     lexer lex(src.text());
//     for (token t = lex.next(); t.kind() != token_kind::end_of_file; t = lex.next()) ...
//     lexer::state saved = lex.save();   lex.restore(saved);   // a checkpoint is a copy
//
// What CPython's tokenizer does, the same way:
//   * Indentation: a tab moves to the next multiple of 8 and a form feed resets the column; the
//     same lines measured with tabs as 1 must agree, or it is a TabError (CPython's two counts).
//     At most 100 levels. Blank and comment-only lines give no tokens.
//   * Newlines inside brackets are not NEWLINE tokens; a backslash joins two lines.
//   * Strings: the prefixes r u b f t and their pairs; bytes hold ASCII only; escapes are checked
//     here (\x, \u, \U, \N{...}) and decoded only when a value is asked for (literals.h).
//   * f- and t-strings (PEP 701, 750): a start token, literal text, `{`, the tokens of the
//     expression, `!`, `:` and the format spec as more text, `}`, the end token. Any quote may
//     nest inside an expression; format specs nest two deep, f-strings 150 deep.
//   * Numbers with underscores; leading zeros refused; a number run into a name refused, but for
//     the keywords CPython lets through with a warning (`1if x else y`).
//   * Brackets nest 200 deep and must match.
// Identifiers: beyond ASCII, a character must be XID_Start or XID_Continue (unicode_tables.gen.h,
// from Python's own unicodedata). Names are kept as written: NFKC is not applied yet (Q37).
//
// The state is a fixed-size value — no heap — so the parser's checkpoint is a copy of it (#280).
//
// CORE layer: pybind-free.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "../../utils/chars.h"
#include "source.h"
#include "tokens.h"
#include "unicode_tables.gen.h"

namespace pygim::code::python {

namespace detail {

/// At most N values, last in first out, held in place: the lexer's nesting is a fixed-size value,
/// so a checkpoint copies it whole and no token allocates. The caller checks full() before a push,
/// and says why it refuses.
template <class T, std::size_t N>
class bounded_stack {
public:
    [[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
    constexpr void push(const T& v) noexcept { m_items[m_size++] = v; }   // not full()
    constexpr void pop() noexcept { --m_size; }                           // not empty()
    [[nodiscard]] constexpr T& top() noexcept { return m_items[m_size - 1]; }
    [[nodiscard]] constexpr const T& top() const noexcept { return m_items[m_size - 1]; }

private:
    std::array<T, N> m_items{};
    std::uint16_t m_size = 0;
};
static_assert([] {
    bounded_stack<int, 2> s;
    s.push(1);
    s.push(2);
    const bool full_at_two = s.full() && s.top() == 2;
    s.pop();
    return full_at_two && s.size() == 1 && s.top() == 1;
}());

/// A bracket open in the source: which one, and where.
class bracket {
public:
    constexpr bracket() = default;
    constexpr bracket(char which, std::uint32_t at) noexcept : m_which(which), m_at(at) {}

    [[nodiscard]] constexpr char which() const noexcept { return m_which; }
    [[nodiscard]] constexpr std::uint32_t at() const noexcept { return m_at; }
    /// Whether `closer` closes it: ')' a '(', ']' a '[', '}' a '{'.
    [[nodiscard]] constexpr bool closed_by(char closer) const noexcept { return m_which == (closer == ')' ? '(' : closer == ']' ? '[' : '{'); }

private:
    char m_which = '(';
    std::uint32_t m_at = 0;
};
static_assert(bracket('[', 4).closed_by(']') && !bracket('[', 4).closed_by(')') && bracket('{', 9).at() == 9);

/// One level of indentation: its column with a tab to the next multiple of 8, and with a tab as
/// one column (CPython's altcol: the two must agree on which of two lines is indented deeper).
class indent_level {
public:
    constexpr indent_level() = default;
    constexpr indent_level(std::uint32_t column, std::uint32_t alt) noexcept : m_column(column), m_alt(alt) {}

    [[nodiscard]] constexpr std::uint32_t column() const noexcept { return m_column; }
    [[nodiscard]] constexpr std::uint32_t alt() const noexcept { return m_alt; }

private:
    std::uint32_t m_column = 0;   // 32 bits: a line indented 70,000 spaces is Python (16 bits made it an unexpected indent)
    std::uint32_t m_alt = 0;
};
static_assert(indent_level(8, 1).column() == 8 && indent_level(8, 1).alt() == 1 && indent_level(70'000, 70'000).column() == 70'000);

/// One f- or t-string being read: its quotes, its prefix, and the replacement fields open in it —
/// a field, and format specs nested in it, innermost last.
class fstring_frame {
public:
    static constexpr std::size_t fields_deep = 3;   // a field, and format specs nested two deep

    constexpr fstring_frame() = default;
    constexpr fstring_frame(char quote, bool triple, bool raw) noexcept : m_quote(quote), m_triple(triple), m_raw(raw) {}

    [[nodiscard]] constexpr char quote() const noexcept { return m_quote; }
    [[nodiscard]] constexpr bool triple() const noexcept { return m_triple; }
    [[nodiscard]] constexpr bool raw() const noexcept { return m_raw; }
    /// Whether a replacement field is open: the text read now is a format spec's.
    [[nodiscard]] constexpr bool in_field() const noexcept { return m_fields != 0; }
    /// Whether text is read now (the string's own or a format spec's), not a field's expression.
    [[nodiscard]] constexpr bool in_text() const noexcept { return m_fields == 0 || m_in_spec[m_fields - 1]; }
    /// Whether the innermost field reads its expression at bracket depth `depth`: there a `}`, a
    /// `:` and a `!` are the field's.
    [[nodiscard]] constexpr bool at_expression_level(std::size_t depth) const noexcept {
        return m_fields != 0 && !m_in_spec[m_fields - 1] && m_depth[m_fields - 1] == depth;
    }
    [[nodiscard]] constexpr bool fields_full() const noexcept { return m_fields == fields_deep; }
    /// Opens a field whose `{` left the brackets `depth` deep (not fields_full()).
    constexpr void open_field(std::size_t depth) noexcept {
        m_depth[m_fields] = static_cast<std::uint16_t>(depth);
        m_in_spec[m_fields] = false;
        ++m_fields;
    }
    constexpr void close_field() noexcept { --m_fields; }
    /// The innermost field's format spec starts: its `:` was read.
    constexpr void start_spec() noexcept { m_in_spec[m_fields - 1] = true; }

private:
    char m_quote = '"';
    bool m_triple = false;
    bool m_raw = false;
    std::uint8_t m_fields = 0;
    std::array<std::uint16_t, fields_deep> m_depth{};   // the bracket depth each field's `{` left
    std::array<bool, fields_deep> m_in_spec{};
};
static_assert([] {
    fstring_frame f('"', false, false);
    f.open_field(1);
    const bool expression = !f.in_text() && f.at_expression_level(1) && !f.at_expression_level(2);
    f.start_spec();
    return expression && f.in_text() && f.in_field() && !f.fields_full();
}());

}  // namespace detail

class lexer {
public:
    static constexpr std::size_t max_indents = 100;   // CPython's MAXINDENT
    static constexpr std::size_t max_brackets = 200;  // MAXLEVEL
    static constexpr std::size_t max_fstrings = 150;  // MAXFSTRINGLEVEL

    /// Everything the lexer is, copied whole for a checkpoint: a memento only the lexer reads.
    class state {
        friend class lexer;
        std::uint32_t pos = 0;
        bool at_line_start = true;
        bool line_has_token = false;        // a token since the last NEWLINE: one is owed at the end
        std::uint16_t pending_dedents = 0;
        detail::bounded_stack<detail::bracket, max_brackets> brackets;
        // column 0's level first, always, and 99 more: CPython refuses its 100th (indent + 1 >= MAXINDENT)
        detail::bounded_stack<detail::indent_level, max_indents> indents;
        // 149 open at most: CPython refuses its 150th (tok_mode_stack_index + 1 >= MAXFSTRINGLEVEL)
        detail::bounded_stack<detail::fstring_frame, max_fstrings - 1> fstrings;
    };

    /// A lexer over `text`, which a NUL must follow (source_text gives such a text).
    explicit lexer(std::string_view text) noexcept : m_text(text.data()), m_size(static_cast<std::uint32_t>(text.size())) {
        m.indents.push(detail::indent_level());
    }

    [[nodiscard]] const state& save() const noexcept { return m; }
    void restore(const state& s) noexcept { m = s; }
    [[nodiscard]] std::string_view text() const noexcept { return {m_text, m_size}; }

    /// The next token; a syntax error raises.
    [[nodiscard]] token next() {
        const token t = scan();
        if (t.kind() == token_kind::newline) m.line_has_token = false;
        else if (t.kind() != token_kind::indent && t.kind() != token_kind::dedent && t.kind() != token_kind::end_of_file) m.line_has_token = true;
        return t;
    }

private:
    // ── characters ──────────────────────────────────────────────────────────
    [[nodiscard]] char at(std::uint32_t i) const noexcept { return i <= m_size ? m_text[i] : '\0'; }
    [[nodiscard]] bool is_newline(std::uint32_t i) const noexcept { return at(i) == '\n' || at(i) == '\r'; }
    [[nodiscard]] std::uint32_t after_newline(std::uint32_t i) const noexcept { return (at(i) == '\r' && at(i + 1) == '\n') ? i + 2 : i + 1; }
    [[nodiscard]] bool at_end(std::uint32_t i) const noexcept { return i >= m_size; }

    [[noreturn]] static void fail(std::string message, std::uint32_t offset) { throw syntax_error(std::move(message), offset); }

    // Whether the innermost f-string is reading text (its literal part or a format spec) now.
    [[nodiscard]] bool in_fstring_text() const noexcept { return !m.fstrings.empty() && m.fstrings.top().in_text(); }

    // ── the token loop ──────────────────────────────────────────────────────
    [[nodiscard]] token scan() {
        if (m.pending_dedents) {
            --m.pending_dedents;
            return {token_kind::dedent, 0, m.pos, m.pos};
        }
        if (in_fstring_text()) return fstring_text();
        if (m.at_line_start && m.brackets.empty()) {
            if (const token t = line_start(); t.kind() != token_kind::count_) return t;
        }
        for (;;) {
            skip_blanks();
            const std::uint32_t start = m.pos;
            const char c = at(start);
            if (c == '\n' || c == '\r') {
                m.pos = after_newline(start);
                if (!m.brackets.empty()) continue;   // inside brackets, and in an f-string's expression: no NEWLINE
                m.at_line_start = true;
                return {token_kind::newline, 0, start, m.pos};
            }
            if (at_end(start)) return end_of_input();
            if (chars::starts_identifier(c)) return name_or_string(start);
            if (chars::is_digit(c) || (c == '.' && chars::is_digit(at(start + 1)))) return number(start);
            if (c == '"' || c == '\'') return string(start, 0);
            return punctuation(start);
        }
    }

    // Indentation at the start of a line: an INDENT, the first DEDENT, or none (kind count_).
    // A backslash in the indentation joins the next line to it, and the column it stood at is the
    // line's indentation (CPython's cont_line_col; at column 0 the count runs on into the next line).
    [[nodiscard]] token line_start() {
        for (;;) {
            std::uint32_t col = 0, alt = 0, cont = 0, p = m.pos;
            for (;;) {
                const char c = at(p);
                if (c == ' ') { ++col; ++alt; ++p; }
                else if (c == '\t') { col = (col / 8 + 1) * 8; ++alt; ++p; }
                else if (c == '\f') { col = alt = 0; ++p; }
                else if (c == '\\' && is_newline(p + 1)) {
                    if (cont == 0) cont = col;
                    p = after_newline(p + 1);
                    if (at_end(p)) fail("unexpected EOF while parsing", p);
                }
                else break;
            }
            if (cont != 0) col = alt = cont;
            const char c = at(p);
            if (c == '#') {   // a comment-only line
                while (!at_end(p) && !is_newline(p)) ++p;
            }
            if (is_newline(p) && !at_end(p)) {   // a blank line: no tokens
                m.pos = after_newline(p);
                continue;
            }
            if (at_end(p)) {   // the end: the dedents come from end_of_input
                m.pos = p;
                m.at_line_start = false;
                return {token_kind::count_, 0, 0, 0};
            }
            m.pos = p;
            m.at_line_start = false;
            return indentation(col, alt, p);
        }
    }

    [[nodiscard]] token indentation(std::uint32_t col, std::uint32_t alt, std::uint32_t p) {
        const detail::indent_level top = m.indents.top();
        if (col == top.column()) {
            if (alt != top.alt()) fail("inconsistent use of tabs and spaces in indentation", p);
            return {token_kind::count_, 0, 0, 0};
        }
        if (col > top.column()) {
            if (m.indents.full()) fail("too many levels of indentation", p);
            if (alt <= top.alt()) fail("inconsistent use of tabs and spaces in indentation", p);
            m.indents.push(detail::indent_level(col, alt));
            return {token_kind::indent, 0, p, p};
        }
        std::uint16_t dedents = 0;
        while (m.indents.size() > 1 && col < m.indents.top().column()) {
            m.indents.pop();
            ++dedents;
        }
        if (col != m.indents.top().column()) fail("unindent does not match any outer indentation level", p);
        if (alt != m.indents.top().alt()) fail("inconsistent use of tabs and spaces in indentation", p);
        m.pending_dedents = static_cast<std::uint16_t>(dedents - 1);
        return {token_kind::dedent, 0, p, p};
    }

    void skip_blanks() {
        for (;;) {
            const char c = at(m.pos);
            if (c == ' ' || c == '\t' || c == '\f') {
                ++m.pos;
            } else if (c == '#') {
                while (!at_end(m.pos) && !is_newline(m.pos)) ++m.pos;
            } else if (c == '\\') {
                if (is_newline(m.pos + 1)) {
                    m.pos = after_newline(m.pos + 1);
                    if (at_end(m.pos)) fail("unexpected EOF while parsing", m.pos);
                } else {
                    fail("unexpected character after line continuation character", m.pos);
                }
            } else {
                return;
            }
        }
    }

    [[nodiscard]] token end_of_input() {
        if (!m.fstrings.empty()) fail("unterminated f-string literal", m.pos);
        if (!m.brackets.empty()) fail(std::string("'") + m.brackets.top().which() + "' was never closed", m.brackets.top().at());
        if (m.line_has_token) return {token_kind::newline, 0, m.pos, m.pos};   // the newline the last line lacked
        if (m.indents.size() > 1) {
            m.pending_dedents = static_cast<std::uint16_t>(m.indents.size() - 2);
            while (m.indents.size() > 1) m.indents.pop();
            return {token_kind::dedent, 0, m.pos, m.pos};
        }
        return {token_kind::end_of_file, 0, m.pos, m.pos};
    }

    // ── names and keywords ──────────────────────────────────────────────────
    [[nodiscard]] token name_or_string(std::uint32_t start) {
        std::uint32_t p = start;
        for (bool first = true;; first = false) {
            const auto c = static_cast<unsigned char>(at(p));
            if (c < 0x80) {
                if (!chars::continues_identifier(static_cast<char>(c))) break;
                ++p;
                continue;
            }
            const auto [cp, len] = code_point(p);   // the text is valid UTF-8 (source_text checked it)
            if (first ? !unicode::starts_identifier(cp) : !unicode::continues_identifier(cp)) {
                fail("invalid character '" + std::string(m_text + p, len) + "' (U+" + code_point_hex(cp) + ")", p);
            }
            p += len;
        }
        const std::string_view word(m_text + start, p - start);
        if ((at(p) == '"' || at(p) == '\'') && word.size() <= 2) {
            if (const int flags = prefix_flags(word); flags >= 0) return string(start, static_cast<std::uint8_t>(flags), p);
        }
        m.pos = p;
        return {keyword_of(word), 0, start, p};
    }

    // The code point at `p` and its length in bytes.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> code_point(std::uint32_t p) const noexcept {
        const auto c = static_cast<unsigned char>(at(p));
        const std::uint32_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        std::uint32_t cp = c & (len == 4 ? 0x07u : len == 3 ? 0x0Fu : 0x1Fu);
        for (std::uint32_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(at(p + k)) & 0x3Fu);
        return {cp, len};
    }
    // A code point in hex as CPython's messages print it (%04X): four digits at least, no more
    // than it needs — 00E9, 1F600 (six were printed: 01F600).
    [[nodiscard]] static std::string code_point_hex(std::uint32_t cp) {
        constexpr std::string_view digits = "0123456789ABCDEF";
        int shift = 12;
        while (shift < 28 && (cp >> (shift + 4)) != 0) shift += 4;
        std::string out;
        for (; shift >= 0; shift -= 4) out.push_back(digits[(cp >> shift) & 15]);
        return out;
    }

    /// What prefix_flags() adds for an `f` or a `t`: no token's flag (those fit 7 bits), only the
    /// sign that a start token, text and fields follow.
    static constexpr int interpolated = 0x80;
    static constexpr int token_flags = interpolated - 1;

    // A string prefix's flags, or -1 when the word is no prefix. `f` and `t` set fstring_start's flags.
    [[nodiscard]] static int prefix_flags(std::string_view w) noexcept {
        int flags = 0;
        bool f = false, t = false, b = false, u = false, r = false;
        for (char c : w) {
            switch (c | 0x20) {
                case 'r': if (r) return -1; r = true; break;
                case 'b': if (b) return -1; b = true; break;
                case 'u': if (u) return -1; u = true; break;
                case 'f': if (f) return -1; f = true; break;
                case 't': if (t) return -1; t = true; break;
                default: return -1;
            }
        }
        if (u && w.size() > 1) return -1;               // u stands alone
        if ((b ? 1 : 0) + (f ? 1 : 0) + (t ? 1 : 0) > 1) return -1;
        if (r) flags |= token_flag::raw;
        if (b) flags |= token_flag::bytes;
        if (u) flags |= token_flag::unicode;
        if (t) flags |= token_flag::template_;
        if (f || t) flags |= interpolated;
        return flags;
    }

    // ── numbers ─────────────────────────────────────────────────────────────
    // Digits of a base with single underscores between them; the position after them.
    [[nodiscard]] std::uint32_t digits(std::uint32_t p, bool (*ok)(char), const char* what) const {
        if (!ok(at(p))) fail(std::string("invalid ") + what + " literal", p);
        for (;;) {
            while (ok(at(p))) ++p;
            if (at(p) != '_') return p;
            if (!ok(at(p + 1))) fail(std::string("invalid ") + what + " literal", p);
            ++p;
        }
    }

    [[nodiscard]] token number(std::uint32_t start) {
        std::uint32_t p = start;
        std::uint8_t flags = 0;
        const char c1 = static_cast<char>(at(p + 1) | 0x20);
        if (at(p) == '0' && (c1 == 'x' || c1 == 'o' || c1 == 'b')) {
            p += 2;
            if (at(p) == '_') ++p;
            if (c1 == 'x') p = digits(p, [](char c) { return chars::is_hex_digit(c); }, "hexadecimal");
            else if (c1 == 'o') p = digits(p, [](char c) { return c >= '0' && c <= '7'; }, "octal");
            else p = digits(p, [](char c) { return c == '0' || c == '1'; }, "binary");
            if (chars::is_digit(at(p))) fail(std::string("invalid digit '") + at(p) + "' in " + (c1 == 'o' ? "octal" : "binary") + " literal", p);
            return end_number(start, p, 0);
        }
        bool zeros_only = true;
        if (at(p) != '.') {
            for (std::uint32_t q = p; chars::is_digit(at(q)) || at(q) == '_'; ++q) {
                if (at(q) != '0' && at(q) != '_') zeros_only = false;
            }
            p = digits(p, [](char c) { return chars::is_digit(c); }, "decimal");
        }
        bool fraction = false;
        if (at(p) == '.') {
            fraction = true;
            ++p;
            if (chars::is_digit(at(p))) p = digits(p, [](char c) { return chars::is_digit(c); }, "decimal");
        }
        if ((at(p) | 0x20) == 'e') {
            std::uint32_t q = p + 1;
            if (at(q) == '+' || at(q) == '-') ++q;
            if (chars::is_digit(at(q))) {
                p = digits(q, [](char c) { return chars::is_digit(c); }, "decimal");
                fraction = true;
            } else if (!keyword_follows(p)) {
                fail("invalid decimal literal", p);
            }
        }
        if (fraction) flags |= token_flag::floating;
        if ((at(p) | 0x20) == 'j') {
            flags |= token_flag::imaginary;
            ++p;
        } else if (!fraction && at(start) == '0' && !zeros_only) {
            fail("leading zeros in decimal integer literals are not permitted; use an 0o prefix for octal integers", start);
        }
        return end_number(start, p, flags);
    }

    // A number ends where a name could not continue it, but for the keywords CPython warns about.
    [[nodiscard]] token end_number(std::uint32_t start, std::uint32_t p, std::uint8_t flags) {
        if (chars::continues_identifier(at(p)) && !keyword_follows(p)) fail("invalid decimal literal", p);
        m.pos = p;
        return {token_kind::number, flags, start, p};
    }
    [[nodiscard]] bool keyword_follows(std::uint32_t p) const noexcept {
        for (std::string_view kw : {"and", "else", "for", "if", "in", "is", "not", "or"}) {
            if (std::string_view(m_text + p, std::min<std::size_t>(kw.size(), m_size - p)) == kw) return true;
        }
        return false;
    }

    // ── strings ─────────────────────────────────────────────────────────────
    // A string or the start of an f-/t-string at `start`, its quote at `quote_at` (after the prefix).
    [[nodiscard]] token string(std::uint32_t start, std::uint8_t prefix, std::uint32_t quote_at = 0) {
        if (quote_at == 0) quote_at = start;
        const char q = at(quote_at);
        const bool triple = at(quote_at + 1) == q && at(quote_at + 2) == q;
        std::uint8_t flags = static_cast<std::uint8_t>((prefix & token_flags) | (triple ? token_flag::triple : 0));
        std::uint32_t p = quote_at + (triple ? 3 : 1);
        if (prefix & interpolated) {   // an f- or t-string: a start token, then text and fields
            if (m.fstrings.full()) fail("too many nested f-strings", start);
            m.fstrings.push(detail::fstring_frame(q, triple, (prefix & token_flag::raw) != 0));
            m.pos = p;
            return {token_kind::fstring_start, flags, start, p};
        }
        const bool raw = (prefix & token_flag::raw) != 0, bytes = (prefix & token_flag::bytes) != 0;
        for (;;) {
            const char c = at(p);
            if (at_end(p)) fail(triple ? "unterminated triple-quoted string literal" : "unterminated string literal", start);
            if (c == q) {
                if (!triple) { ++p; break; }
                if (at(p + 1) == q && at(p + 2) == q) { p += 3; break; }
                ++p;
                continue;
            }
            if (c == '\\') {
                if (at_end(p + 1)) fail("unterminated string literal", start);
                if (is_newline(p + 1)) { p = after_newline(p + 1); continue; }
                if (!raw) check_escape(p, bytes);
                p += 2;
                continue;
            }
            if ((c == '\n' || c == '\r') && !triple) fail("unterminated string literal", start);
            if (bytes && static_cast<unsigned char>(c) >= 0x80) fail("bytes can only contain ASCII literal characters", p);
            ++p;
        }
        m.pos = p;
        return {token_kind::string, flags, start, p};
    }

    // An escape at `p` (a backslash): \x, \u, \U and \N{...} must be whole; the rest pass.
    void check_escape(std::uint32_t p, bool bytes) const {
        const char e = at(p + 1);
        auto hex_run = [&](std::uint32_t from, int n) {
            for (int i = 0; i < n; ++i) {
                if (!chars::is_hex_digit(at(from + i))) return false;
            }
            return true;
        };
        if (e == 'x' && !hex_run(p + 2, 2)) fail(std::string(bytes ? "(value error)" : "(unicode error) 'unicodeescape' codec can't decode bytes:") + " truncated \\xXX escape", p);
        if (bytes) return;
        if (e == 'u' && !hex_run(p + 2, 4)) fail("(unicode error) 'unicodeescape' codec can't decode bytes: truncated \\uXXXX escape", p);
        if (e == 'U') {
            if (!hex_run(p + 2, 8)) fail("(unicode error) 'unicodeescape' codec can't decode bytes: truncated \\UXXXXXXXX escape", p);
            std::uint32_t v = 0;
            for (int i = 0; i < 8; ++i) v = v * 16 + static_cast<std::uint32_t>(chars::hex_value(at(p + 2 + i)));   // hex_run checked them
            if (v > 0x10FFFF) fail("(unicode error) 'unicodeescape' codec can't decode bytes: illegal Unicode character", p);
        }
        if (e == 'N') {
            if (at(p + 2) != '{') fail("(unicode error) 'unicodeescape' codec can't decode bytes: malformed \\N character escape", p);
            std::uint32_t q = p + 3;
            while (!at_end(q) && at(q) != '}' && at(q) != '\n' && at(q) != '\r' && at(q) != '"' && at(q) != '\'') ++q;
            if (at(q) != '}' || q == p + 3) fail("(unicode error) 'unicodeescape' codec can't decode bytes: malformed \\N character escape", p);
        }
    }

    // The literal text of the innermost f-string, or of a format spec: a middle token, the field's
    // `{` or (in a spec) its `}`, or the end of the f-string.
    [[nodiscard]] token fstring_text() {
        detail::fstring_frame& f = m.fstrings.top();
        const bool spec = f.in_field();
        const std::uint32_t start = m.pos;
        std::uint32_t p = start;
        for (;;) {
            const char c = at(p);
            if (at_end(p)) fail(spec ? "f-string: expecting '}'" : "unterminated f-string literal", p);
            if (c == f.quote() && (!f.triple() || (at(p + 1) == c && at(p + 2) == c))) {
                if (spec) fail("f-string: expecting '}'", p);
                if (p > start) return middle(start, p);
                m.pos = p + (f.triple() ? 3 : 1);
                m.fstrings.pop();
                return {token_kind::fstring_end, 0, p, m.pos};
            }
            if (c == '\\') {
                const char e = at(p + 1);
                if (e == '{' || e == '}') { ++p; continue; }             // the brace is not escaped
                if (!f.raw() && e == 'N' && at(p + 2) == '{') {              // \N{NAME}: its braces are the escape's
                    check_escape(p, false);
                    while (at(p) != '}') ++p;
                    ++p;
                    continue;
                }
                if (at_end(p + 1)) fail("unterminated f-string literal", p);
                if (is_newline(p + 1)) {   // a backslash joins the lines, in either kind of quotes
                    p = after_newline(p + 1);
                    continue;
                }
                if (!f.raw()) check_escape(p, false);
                p += 2;
                continue;
            }
            if (c == '{') {
                if (!spec && at(p + 1) == '{') { p += 2; continue; }       // {{ is a brace
                if (p > start) return middle(start, p);
                if (f.fields_full()) fail("f-string: expressions nested too deeply", p);
                open_bracket('{', p);
                f.open_field(m.brackets.size());
                m.pos = p + 1;
                return {token_kind::lbrace, 0, p, p + 1};
            }
            if (c == '}') {
                if (!spec) {
                    if (at(p + 1) == '}') { p += 2; continue; }             // }} is a brace
                    fail("f-string: single '}' is not allowed", p);
                }
                if (p > start) return middle(start, p);
                return close_field(p);
            }
            if (c == '\n' || c == '\r') {
                if (!f.triple()) fail(spec ? "f-string: newlines are not allowed in format specifiers for single quoted f-strings" : "unterminated f-string literal", p);
                p = after_newline(p);
                continue;
            }
            ++p;
        }
    }

    [[nodiscard]] token middle(std::uint32_t start, std::uint32_t end) {
        m.pos = end;
        return {token_kind::fstring_middle, 0, start, end};
    }

    // The `}` that closes the innermost f-string's innermost field, at `p`.
    [[nodiscard]] token close_field(std::uint32_t p) {
        m.fstrings.top().close_field();
        m.brackets.pop();
        m.pos = p + 1;
        return {token_kind::rbrace, 0, p, p + 1};
    }

    // ── operators and delimiters ────────────────────────────────────────────
    void open_bracket(char c, std::uint32_t p) {
        if (m.brackets.full()) fail("too many nested parentheses", p);
        m.brackets.push(detail::bracket(c, p));
    }
    void close_bracket(char c, std::uint32_t p) {
        if (m.brackets.empty()) fail(std::string("unmatched '") + c + "'", p);
        if (!m.brackets.top().closed_by(c)) {
            fail(std::string("closing parenthesis '") + c + "' does not match opening parenthesis '" + m.brackets.top().which() + "'", p);
        }
        m.brackets.pop();
    }

    // The field's own depth, when the innermost f-string is reading the expression of a field.
    [[nodiscard]] bool at_field_level() const noexcept {
        return !m.fstrings.empty() && m.fstrings.top().at_expression_level(m.brackets.size());
    }

    [[nodiscard]] token punctuation(std::uint32_t s) {
        const char c = at(s), c1 = at(s + 1), c2 = at(s + 2);
        auto op = [&](token_kind k, std::uint32_t len) {
            m.pos = s + len;
            return token(k, 0, s, s + len);
        };
        switch (c) {
            case '(': open_bracket('(', s); return op(token_kind::lpar, 1);
            case '[': open_bracket('[', s); return op(token_kind::lsqb, 1);
            case '{': open_bracket('{', s); return op(token_kind::lbrace, 1);
            case ')': close_bracket(')', s); return op(token_kind::rpar, 1);
            case ']': close_bracket(']', s); return op(token_kind::rsqb, 1);
            case '}':
                if (at_field_level()) return close_field(s);
                close_bracket('}', s);
                return op(token_kind::rbrace, 1);
            case ':':
                if (at_field_level()) {   // a field's format spec starts, `:=` or not
                    m.fstrings.top().start_spec();
                    return op(token_kind::colon, 1);
                }
                return c1 == '=' ? op(token_kind::colonequal, 2) : op(token_kind::colon, 1);
            case ',': return op(token_kind::comma, 1);
            case ';': return op(token_kind::semi, 1);
            case '~': return op(token_kind::tilde, 1);
            case '+': return c1 == '=' ? op(token_kind::plusequal, 2) : op(token_kind::plus, 1);
            case '-':
                if (c1 == '=') return op(token_kind::minequal, 2);
                return c1 == '>' ? op(token_kind::rarrow, 2) : op(token_kind::minus, 1);
            case '*':
                if (c1 == '*') return c2 == '=' ? op(token_kind::doublestarequal, 3) : op(token_kind::doublestar, 2);
                return c1 == '=' ? op(token_kind::starequal, 2) : op(token_kind::star, 1);
            case '/':
                if (c1 == '/') return c2 == '=' ? op(token_kind::doubleslashequal, 3) : op(token_kind::doubleslash, 2);
                return c1 == '=' ? op(token_kind::slashequal, 2) : op(token_kind::slash, 1);
            case '%': return c1 == '=' ? op(token_kind::percentequal, 2) : op(token_kind::percent, 1);
            case '&': return c1 == '=' ? op(token_kind::amperequal, 2) : op(token_kind::amper, 1);
            case '|': return c1 == '=' ? op(token_kind::vbarequal, 2) : op(token_kind::vbar, 1);
            case '^': return c1 == '=' ? op(token_kind::circumflexequal, 2) : op(token_kind::circumflex, 1);
            case '@': return c1 == '=' ? op(token_kind::atequal, 2) : op(token_kind::at, 1);
            case '<':
                if (c1 == '<') return c2 == '=' ? op(token_kind::leftshiftequal, 3) : op(token_kind::leftshift, 2);
                if (c1 == '>') fail("invalid syntax", s);
                return c1 == '=' ? op(token_kind::lessequal, 2) : op(token_kind::less, 1);
            case '>':
                if (c1 == '>') return c2 == '=' ? op(token_kind::rightshiftequal, 3) : op(token_kind::rightshift, 2);
                return c1 == '=' ? op(token_kind::greaterequal, 2) : op(token_kind::greater, 1);
            case '=': return c1 == '=' ? op(token_kind::eqequal, 2) : op(token_kind::equal, 1);
            case '!':
                if (c1 == '=') return op(token_kind::notequal, 2);
                if (!m.fstrings.empty() && m.fstrings.top().in_field()) return op(token_kind::exclamation, 1);
                fail("invalid syntax", s);
            case '.':
                if (c1 == '.' && c2 == '.') return op(token_kind::ellipsis, 3);
                return op(token_kind::dot, 1);
            default:
                fail(std::string("invalid character '") + c + "'", s);
        }
    }

    const char* m_text;
    std::uint32_t m_size;
    state m;
};

}  // namespace pygim::code::python
