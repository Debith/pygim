#pragma once
// code/python/cursor.h — where the parser stands in the token stream: the current token and the
// one after it, pulled from the lexer as the grammar looks, and the refusals that name the token
// standing there.
//
//     token_cursor<stop_at_first> c(text);
//     if (c.at(token_kind::kw_def)) c.advance();
//     const token n = c.expect_name();
//     const auto saved = c.save();  ...  c.restore(saved);   // a speculative parse undone
//
// CORE layer: pybind-free.

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

#include "lexer.h"
#include "tokens.h"

namespace pygim::code::python {

template <class Errors>
class token_cursor {
public:
    /// How far past the current token the grammar is ever read: one (`name =` in a call, `match x`,
    /// `async for`). The ring holds the current token and that one.
    static constexpr std::uint32_t look_ahead = 1;
    static constexpr std::uint32_t ring_size = look_ahead + 1;
    static_assert(std::has_single_bit(ring_size), "the ring's indices wrap by a mask");

    /// Where the cursor stood: a memento only the cursor reads (D58).
    class state {
        friend class token_cursor;
        state(const lexer::state& lex, const std::array<token, ring_size>& ring, std::uint32_t head, std::uint32_t filled, std::uint32_t prev_end) noexcept
            : m_lex(lex), m_ring(ring), m_head(head), m_filled(filled), m_prev_end(prev_end) {}
        lexer::state m_lex;
        std::array<token, ring_size> m_ring;
        std::uint32_t m_head, m_filled, m_prev_end;
    };

    explicit token_cursor(std::string_view text) : m_text(text), m_lex(text) {}

    [[nodiscard]] const token& peek(std::uint32_t n) {   // n ≤ look_ahead
        while (m_filled <= n) {
            m_ring[(m_head + m_filled) & (ring_size - 1)] = m_lex.next();
            ++m_filled;
        }
        return m_ring[(m_head + n) & (ring_size - 1)];
    }
    [[nodiscard]] const token& tok() { return peek(0); }
    [[nodiscard]] token_kind kind_at(std::uint32_t n) { return peek(n).kind(); }
    [[nodiscard]] bool at(token_kind k) { return tok().kind() == k; }
    token advance() {
        const token t = tok();
        m_prev_end = t.end();
        m_head = (m_head + 1) & (ring_size - 1);
        --m_filled;
        return t;
    }
    bool eat(token_kind k) {
        if (!at(k)) return false;
        advance();
        return true;
    }
    token expect(token_kind k) {
        if (!at(k)) fail_expected(k);
        return advance();
    }
    [[nodiscard]] bool at_name() { return info(tok().kind()).name_like(); }
    token expect_name() {
        if (!at_name()) fail_here("invalid syntax: expected a name");
        return advance();
    }
    /// Whether a value follows (`return x` against `return`).
    [[nodiscard]] bool starts_value() { return info(tok().kind()).starts_expression(); }

    /// Where the token read last ends: a node's end, when it closes there.
    [[nodiscard]] std::uint32_t prev_end() const noexcept { return m_prev_end; }
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }
    [[nodiscard]] std::string_view text_of(const token& t) const noexcept { return t.text(m_text); }

    [[noreturn]] void fail_expected(token_kind k) {
        if (k == token_kind::indent) Errors::fail("expected an indented block", tok().start());
        const bool phrase = k < token_kind::kw_False;   // "a name", "a new line": what stands there, not its text
        const std::string what(spelling(k));
        Errors::fail("invalid syntax: expected " + (phrase ? what : "'" + what + "'"), tok().start());
    }
    [[noreturn]] void fail_here(std::string_view what = "invalid syntax") { Errors::fail(std::string(what), tok().start()); }

    [[nodiscard]] state save() const { return {m_lex.save(), m_ring, m_head, m_filled, m_prev_end}; }
    void restore(const state& s) {
        m_lex.restore(s.m_lex);
        m_ring = s.m_ring;
        m_head = s.m_head;
        m_filled = s.m_filled;
        m_prev_end = s.m_prev_end;
    }

private:
    std::string_view m_text;
    lexer m_lex;
    std::array<token, ring_size> m_ring{};
    std::uint32_t m_head = 0, m_filled = 0;
    std::uint32_t m_prev_end = 0;
};

}  // namespace pygim::code::python
