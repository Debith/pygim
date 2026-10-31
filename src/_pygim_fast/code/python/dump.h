#pragma once
// code/python/dump.h — a tree, or a node of it, written as Python 3.14's ast.dump writes it.
//
//     dump(tree, names, plain_repr{}, 0)        -> "Module(body=[Expr(value=Name(id='x', ctx=Load()))])"
//     dump(tree, names, repr, 0, true)          -> ... with lineno=, col_offset=, end_lineno=, end_col_offset=
//
// The text is ast.dump's default (show_empty=False): a field that is None or [] is left out, but
// for Constant's and MatchSingleton's value. It is how the parser is checked against CPython — the
// two dumps must be equal, file by file — and a readable form of any subtree.
//
// How a constant is written is the caller's (the `Repr` policy): the adapter asks CPython for
// repr() of the value; plain_repr writes it in C++ and is exact for ASCII text and decimal
// numbers, the cases a diagnostic needs.
//
// CORE layer: pybind-free.

#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../utils/byte_scan.h"
#include "ast_tables.gen.h"
#include "literals.h"
#include "rows.h"
#include "tree.h"

namespace pygim::code::python {

/// How a dump writes values: repr() of a str (given as WTF-8), of bytes, of a number from its
/// digits, of an identifier; and the character a \N{NAME} escape names.
template <class R>
concept constant_repr = requires(const R& r, std::string_view s, std::string& out) {
    r.str(s, out);
    r.bytes(s, out);
    r.integer(s, out);
    r.floating(s, out);
    r.imaginary(s, out);
    { r.character(s) } -> std::same_as<std::optional<std::uint32_t>>;
};

/// repr() in C++: exact for ASCII text, for bytes and for decimal integers; other numbers as written.
class plain_repr {
public:
    /// Whether str(s, out) writes what CPython's repr() does: text all ASCII.
    [[nodiscard]] static constexpr bool exact_str(std::string_view s) noexcept { return byte_scan::first_non_ascii(s) == s.size(); }
    /// Whether integer(s, out) does: decimal digits, no leading zero (but 0 itself), no underscore.
    [[nodiscard]] static constexpr bool exact_integer(std::string_view s) noexcept {
        if (s.empty() || (s[0] == '0' && s.size() > 1)) return false;
        for (char c : s) {
            if (c < '0' || c > '9') return false;
        }
        return true;
    }

    void str(std::string_view s, std::string& out) const { quoted(s, out, false); }
    void bytes(std::string_view s, std::string& out) const {
        out.push_back('b');
        quoted(s, out, true);
    }
    void integer(std::string_view s, std::string& out) const { out.append(s); }
    void floating(std::string_view s, std::string& out) const { out.append(s); }
    void imaginary(std::string_view s, std::string& out) const { out.append(s); }
    [[nodiscard]] std::optional<std::uint32_t> character(std::string_view) const noexcept { return std::nullopt; }

private:
    static void quoted(std::string_view s, std::string& out, bool bytes) {
        const bool single = s.find('\'') == std::string_view::npos || s.find('"') != std::string_view::npos;
        const char q = single ? '\'' : '"';
        out.push_back(q);
        constexpr std::string_view hex = "0123456789abcdef";
        for (char ch : s) {
            const auto c = static_cast<unsigned char>(ch);
            if (ch == q || ch == '\\') { out.push_back('\\'); out.push_back(ch); }
            else if (ch == '\n') out.append("\\n");
            else if (ch == '\r') out.append("\\r");
            else if (ch == '\t') out.append("\\t");
            else if (c < 0x20 || c == 0x7F || (bytes && c >= 0x80)) {
                out.append("\\x");
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            } else {
                out.push_back(ch);
            }
        }
        out.push_back(q);
    }
};
static_assert(constant_repr<plain_repr>);
static_assert(plain_repr::exact_str("x = 'a'\n") && !plain_repr::exact_str("\xc3\xa9") && plain_repr::exact_integer("120") &&
              plain_repr::exact_integer("0") && !plain_repr::exact_integer("007") && !plain_repr::exact_integer("1_000") && !plain_repr::exact_integer("0x1f"));

namespace detail {

template <mapping::interner Names, constant_repr Repr>
class dumper {
public:
    dumper(const syntax_tree& t, const Names& names, const Repr& repr, bool attributes, std::string& out)
        : m_t(t), m_names(names), m_repr(repr), m_attributes(attributes), m_out(out) {}

    /// Node `i` and everything under it, with a stack of its own: a tree may be as deep as its
    /// source chains (`a.b.b.b...` 100,000 deep is Python, which ast.dump's recursion refuses).
    /// An open node is a frame over its pieces, its text cut where each child's goes; the pieces
    /// and their text are two stacks of the dumper's that keep their room, so a node costs no
    /// allocation of its own.
    void run(std::uint32_t root) {
        open(root);
        while (!m_frames.empty()) {
            frame& f = m_frames.back();
            if (f.done()) {
                m_pieces.erase(m_pieces.begin() + static_cast<std::ptrdiff_t>(f.first()), m_pieces.end());
                m_texts.resize(f.text_from());
                m_frames.pop_back();
                continue;
            }
            const piece p = m_pieces[f.take()];
            m_out.append(p.text(m_texts));
            if (p.child() != no_value) open(p.child());
        }
    }

private:
    // A node's text up to a child, then that child (no_value after the last text): a span of m_texts.
    class piece {
    public:
        piece(std::size_t from, std::size_t to, std::uint32_t child) noexcept : m_from(from), m_to(to), m_child(child) {}
        [[nodiscard]] std::string_view text(const std::string& texts) const noexcept { return std::string_view(texts).substr(m_from, m_to - m_from); }
        [[nodiscard]] std::uint32_t child() const noexcept { return m_child; }

    private:
        std::size_t m_from, m_to;
        std::uint32_t m_child;
    };
    // An open node: its pieces in m_pieces from first() on, the next to write, where its text starts.
    class frame {
    public:
        frame(std::size_t first, std::size_t end, std::size_t text_from) noexcept : m_first(first), m_end(end), m_next(first), m_text_from(text_from) {}
        [[nodiscard]] bool done() const noexcept { return m_next == m_end; }
        [[nodiscard]] std::size_t take() noexcept { return m_next++; }
        [[nodiscard]] std::size_t first() const noexcept { return m_first; }
        [[nodiscard]] std::size_t text_from() const noexcept { return m_text_from; }

    private:
        std::size_t m_first, m_end, m_next, m_text_from;
    };

    // Node `i` written as its pieces, a frame over them on top.
    void open(std::uint32_t i) {
        const std::size_t first = m_pieces.size(), text_from = m_texts.size();
        m_cut = text_from;
        node(i);
        m_pieces.emplace_back(m_cut, m_texts.size(), no_value);
        m_frames.emplace_back(first, m_pieces.size(), text_from);
    }
    // Where child `c`'s text goes: the text since the last cut is a piece.
    void cut(std::uint32_t c) {
        m_pieces.emplace_back(m_cut, m_texts.size(), c);
        m_cut = m_texts.size();
    }
    [[nodiscard]] std::string& text() noexcept { return m_texts; }

    void node(std::uint32_t i) {
        const python::node& n = m_t[i];
        if (n.kind() == kind::absent) {
            text().append("None");
            return;
        }
        const kind_info& k = info(n.kind());
        text().append(k.name());
        text().push_back('(');
        bool first = true;
        auto name = [&](std::string_view field) {
            if (!first) text().append(", ");
            first = false;
            text().append(field);
            text().push_back('=');
        };
        for (std::size_t s = 0; s < k.fields().size(); ++s) {
            const field_info& f = k.fields()[s];
            switch (f.storage()) {
                case storage::never: break;
                case storage::child: children(i, static_cast<std::uint8_t>(s), f, name); break;
                case storage::value: value(i, f, name); break;
                case storage::small: small(i, f, name); break;
                case storage::leaf: leaves(i, static_cast<std::uint8_t>(s), f, name); break;
            }
        }
        if (m_attributes && k.positioned()) {
            const position a = m_t.position_of(n.start()), b = m_t.position_of(n.end());
            name("lineno"); number(a.line());
            name("col_offset"); number(a.column());
            name("end_lineno"); number(b.line());
            name("end_col_offset"); number(b.column());
        }
        text().push_back(')');
    }

    void number(std::int64_t v) {
        std::array<char, 24> buf{};
        const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v);
        text().append(buf.data(), r.ptr);
    }
    void identifier(std::uint32_t id) { m_repr.str(m_names[id], text()); }

    template <class Name>
    void children(std::uint32_t i, std::uint8_t s, const field_info& f, Name& name) {
        std::size_t count = 0;
        for (std::uint32_t c : m_t.children(i)) count += m_t[c].field() == s;
        if (count == 0) return;
        name(f.name());
        const bool list = f.shape() == shape::nodes;
        if (list) text().push_back('[');
        bool first = true;
        for (std::uint32_t c : m_t.children(i)) {
            if (m_t[c].field() != s) continue;
            if (!first) text().append(", ");
            first = false;
            cut(c);   // the child's text goes here
        }
        if (list) text().push_back(']');
    }

    template <class Name>
    void value(std::uint32_t i, const field_info& f, Name& name) {
        const python::node& n = m_t[i];
        if (f.shape() == shape::constant) {   // Constant.value, Interpolation.str
            if (n.kind() == kind::Interpolation) {
                name(f.name());
                std::string s;
                decode_text(m_t, n.value(), s, chars());
                m_repr.str(s, text());
                return;
            }
            name(f.name());
            constant(i);
            return;
        }
        if (n.value() == no_value) return;   // an optional identifier left out
        name(f.name());
        identifier(n.value());
    }

    // Constant.value of node i, or a MatchSingleton's (None, True, False: no text of its own).
    void constant(std::uint32_t i) {
        const python::node& n = m_t[i];
        const constant_kind k = constant_of(n.small());
        switch (k) {
            case constant_kind::none: text().append("None"); return;
            case constant_kind::true_: text().append("True"); return;
            case constant_kind::false_: text().append("False"); return;
            case constant_kind::ellipsis: text().append("Ellipsis"); return;
            case constant_kind::integer: m_repr.integer(number_text(m_t, i), text()); return;
            case constant_kind::floating: m_repr.floating(number_text(m_t, i), text()); return;
            case constant_kind::imaginary: {
                std::string digits = number_text(m_t, i);
                digits.pop_back();   // the j
                m_repr.imaginary(digits, text());
                return;
            }
            case constant_kind::string:
            case constant_kind::bytes: {
                std::string s;
                decode_text(m_t, n.value(), s, chars());
                if (k == constant_kind::bytes) m_repr.bytes(s, text());
                else m_repr.str(s, text());
                return;
            }
        }
    }

    template <class Name>
    void small(std::uint32_t i, const field_info& f, Name& name) {
        const python::node& n = m_t[i];
        switch (meaning_of(n.kind(), f)) {
            case small_meaning::string_prefix:   // 'u' or nothing
                if (!has_unicode_prefix(n.small())) return;
                name(f.name());
                text().append("'u'");
                return;
            case small_meaning::member:
                name(f.name());
                text().append(info(member_of(f, n.small())).name());
                text().append("()");
                return;
            case small_meaning::singleton:
                name(f.name());
                constant(i);
                return;
            case small_meaning::conversion:
                name(f.name());
                number(conversion_of(n.small()));
                return;
            case small_meaning::number:
                name(f.name());
                number(n.small());
                return;
        }
    }

    template <class Name>
    void leaves(std::uint32_t i, std::uint8_t s, const field_info& f, Name& name) {
        std::size_t count = 0;
        for (std::uint32_t c : m_t.children(i)) count += m_t[c].field() == s;
        if (count == 0) return;
        name(f.name());
        if (f.shape() == shape::optional_identifier) {
            for (std::uint32_t c : m_t.children(i)) {
                if (m_t[c].field() == s) identifier(m_t[c].value());
            }
            return;
        }
        text().push_back('[');
        bool first = true;
        for (std::uint32_t c : m_t.children(i)) {
            if (m_t[c].field() != s) continue;
            if (!first) text().append(", ");
            first = false;
            if (f.shape() == shape::operators) {
                text().append(info(member_of(f, m_t[c].small())).name());
                text().append("()");
            } else {
                identifier(m_t[c].value());
            }
        }
        text().push_back(']');
    }

    [[nodiscard]] auto chars() const {
        return [this](std::string_view name) { return m_repr.character(name); };
    }

    const syntax_tree& m_t;
    const Names& m_names;
    const Repr& m_repr;
    bool m_attributes;
    std::string& m_out;
    std::vector<frame> m_frames;
    std::vector<piece> m_pieces;
    std::string m_texts;
    std::size_t m_cut = 0;   // where the text of the piece being written starts
};

}  // namespace detail

/// Node `i` of `tree` and everything under it, as ast.dump writes it.
template <mapping::interner Names, constant_repr Repr>
[[nodiscard]] std::string dump(const syntax_tree& tree, const Names& names, const Repr& repr, std::uint32_t i = 0, bool attributes = false) {
    std::string out;
    detail::dumper<Names, Repr>(tree, names, repr, attributes, out).run(i);
    return out;
}

}  // namespace pygim::code::python
