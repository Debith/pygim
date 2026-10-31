#pragma once
// code/python/grammar/strings.h — strings side by side as one node, and f- and t-strings with
// their replacement fields.
//
// CORE layer: pybind-free.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "part.h"
#include "string_pieces.h"

namespace pygim::code::python::grammar {

template <class Parser>
class string_rules : public part_of<Parser, string_rules<Parser>> {
    using base = part_of<Parser, string_rules<Parser>>;
    using base::tok;
    using base::at;
    using base::advance;
    using base::expect;
    using base::fail_here;
    using base::at_name;
    using base::prev_end;
    using base::text_of;
    using base::source;
    using base::out;
    using base::rows;
    using base::fail;
    using base::nest;
    using base::parser;

public:
    using pending_text = string_pieces::pending_text;

    // Strings side by side, as one node: a Constant, or a JoinedStr / TemplateStr when an f- or
    // t-string is among them; their text runs merged into Constants as CPython merges them.
    expr strings() {
        const std::uint32_t first = rows().mark();
        const std::uint32_t start = tok().start();
        pending_text text = m_pieces.open();
        bool any_f = false, any_t = false, any_bytes = false, any_str = false;
        while (at(token_kind::string) || at(token_kind::fstring_start)) {
            const token t = tok();
            const bool is_t = t.kind() == token_kind::fstring_start && t.has(token_flag::template_);
            const bool is_bytes = t.has(token_flag::bytes);
            if (is_t) any_t = true;
            else if (t.kind() == token_kind::fstring_start) any_f = true;
            if (is_bytes) any_bytes = true;
            else if (!is_t) any_str = true;
            if (any_t && (any_f || any_bytes || any_str)) fail("cannot mix t-string literals with string or bytes literals", t.start());
            if (any_bytes && (any_str || any_f)) fail("cannot mix bytes and nonbytes literals", t.start());
            if (t.kind() == token_kind::string) {
                advance();
                const std::uint32_t quote = string_body_start(t);
                const std::uint32_t quotes = t.has(token_flag::triple) ? 3 : 1;
                const std::uint8_t flags = static_cast<std::uint8_t>((t.has(token_flag::raw) ? piece_flag::raw : 0) | (is_bytes ? piece_flag::bytes : 0));
                m_pieces.add(text, quote, t.end() - quotes, flags, t.start(), t.end(), t.has(token_flag::unicode));
            } else {
                fstring(text, t);
            }
        }
        if (any_f || any_t) {
            m_pieces.flush(source(), out(), text, rows().mark(), slot::JoinedStr::values, false);
            const enum kind k = any_t ? kind::TemplateStr : kind::JoinedStr;
            return {rows().close(k, first, start, prev_end()), first, start, prev_end()};
        }
        const std::uint16_t small = static_cast<std::uint16_t>(any_bytes ? constant_kind::bytes : constant_kind::string) | (text.unicode() ? unicode_prefix : 0);
        const std::uint32_t lit = m_pieces.commit(out(), text);
        return {rows().close(kind::Constant, first, start, prev_end(), small, lit), first, start, prev_end()};
    }
    // Where a string token's text starts: after its prefix and opening quotes.
    [[nodiscard]] std::uint32_t string_body_start(const token& t) {
        const std::string_view written = source();
        std::uint32_t p = t.start();
        while (written[p] != '"' && written[p] != '\'') ++p;
        return p + (t.has(token_flag::triple) ? 3 : 1);
    }

    // One f- or t-string from its start token to its end token: its text into `text`, each field
    // a FormattedValue or Interpolation row.
    void fstring(pending_text& text, const token& start) {
        const bool raw = start.has(token_flag::raw), is_t = start.has(token_flag::template_);
        const std::uint8_t flags = static_cast<std::uint8_t>(piece_flag::fstring | (raw ? piece_flag::raw : 0));
        advance();
        for (;;) {
            const token t = tok();
            if (t.kind() == token_kind::fstring_middle) {
                advance();
                m_pieces.add(text, t.start(), t.end(), flags, t.start(), t.end(), false);
            } else if (t.kind() == token_kind::lbrace) {
                replacement_field(text, is_t ? slot::TemplateStr::values : slot::JoinedStr::values, is_t, flags);
            } else if (t.kind() == token_kind::fstring_end) {
                advance();
                return;
            } else {
                fail_here("f-string: expecting '}'");
            }
        }
    }

    // `{expr = !r:spec}` in an f-string (or a format spec, `text` then the spec's own text).
    void replacement_field(pending_text& text, std::uint8_t field, bool is_t, std::uint8_t text_flags) {
        const auto guard = nest();
        const token l = advance();
        const std::uint32_t at_row = rows().mark();
        if (at(token_kind::rbrace)) fail_here("f-string: valid expression required before '}'");
        const expr e = parser().annotated_rhs();
        const std::uint32_t expr_end = prev_end();
        bool debug = false;
        if (at(token_kind::equal)) {
            debug = true;
            advance();
            m_pieces.add(text, l.end(), tok().start(), piece_flag::verbatim | piece_flag::code, l.end(), tok().start(), false);
        }
        std::int16_t conversion = -1;
        if (at(token_kind::exclamation)) {
            const token bang = advance();
            const token c = tok();
            if (!at_name() || c.start() != bang.end()) fail_here("f-string: conversion type must come right after the exclamation mark");
            const std::string_view s = text_of(c);
            if (s != "s" && s != "r" && s != "a") fail("f-string: invalid conversion character '" + std::string(s) + "': expected 's', 'r', or 'a'", c.start());
            conversion = static_cast<std::int16_t>(s[0]);
            advance();
        }
        std::uint32_t spec_row = no_value;
        if (at(token_kind::colon)) spec_row = format_spec(text_flags);
        const token r = expect(token_kind::rbrace);
        if (debug && conversion == -1 && spec_row == no_value) conversion = 'r';
        const bool shifted = m_pieces.flush(source(), out(), text, at_row, field, false);
        const std::uint32_t shift = shifted ? 1 : 0;
        rows().slot(e.row() + shift, is_t ? slot::Interpolation::value : slot::FormattedValue::value);
        if (spec_row != no_value) rows().slot(spec_row + shift, is_t ? slot::Interpolation::format_spec : slot::FormattedValue::format_spec);
        std::uint32_t value = no_value;
        if (is_t) {   // the expression's text, as written
            const literal_piece written(l.end(), expr_end, piece_flag::verbatim | piece_flag::code);
            value = out().add_literal(std::span<const literal_piece>(&written, 1));
        }
        const std::uint32_t node = rows().close(is_t ? kind::Interpolation : kind::FormattedValue, at_row + shift, l.start(), r.end(), static_cast<std::uint16_t>(conversion), value);
        rows().slot(node, field);
    }

    // A format spec after its `:`: a JoinedStr of its text and nested fields, up to the `}`.
    std::uint32_t format_spec(std::uint8_t text_flags) {
        const std::uint32_t first = rows().mark();
        const token colon = advance();
        pending_text text = m_pieces.open();
        for (;;) {
            const token t = tok();
            if (t.kind() == token_kind::fstring_middle) {
                advance();
                m_pieces.add(text, t.start(), t.end(), text_flags, t.start(), t.end(), false);
            } else if (t.kind() == token_kind::lbrace) {
                replacement_field(text, slot::JoinedStr::values, false, text_flags);
            } else {
                break;
            }
        }
        m_pieces.flush(source(), out(), text, rows().mark(), slot::JoinedStr::values, false);
        const std::uint32_t end = tok().start();
        return rows().close(kind::JoinedStr, first, colon.start(), end);
    }

    /// How many text pieces are pending, for a checkpoint; and back to that many.
    [[nodiscard]] std::size_t pending_pieces() const noexcept { return m_pieces.size(); }
    void drop_pieces_from(std::size_t count) { m_pieces.resize(count); }

private:
    string_pieces m_pieces;   // text pieces of the strings being read, innermost last
};

}  // namespace pygim::code::python::grammar
