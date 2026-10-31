#pragma once
// code/python/grammar/part.h — what a part of the grammar reads through the parser it is part of:
// the token cursor, the rows and names being written, the depth guard and checkpoints. Each part
// (statements.h, compound.h, patterns.h, expressions.h, atoms.h, strings.h) is a class template on
// the parser, deriving part_of<Parser, itself>, and names the helpers it uses; it calls another
// part's rule through parser(), so the places where one part hands over to another are the ones
// that say so. The parser (parser.h) derives every part privately: its face is module() alone.
//
// CORE layer: pybind-free.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "../cursor.h"
#include "../output.h"
#include "targets.h"
#include "vocabulary.h"

namespace pygim::code::python::grammar {

template <class Parser, class Part>
class part_of {
protected:
    /// The parser this part is part of (CRTP: Part derives part_of, Parser derives Part).
    [[nodiscard]] Parser& parser() noexcept { return static_cast<Parser&>(static_cast<Part&>(*this)); }

    // ── the cursor's ──
    [[nodiscard]] const token& peek(std::uint32_t n) { return parser().cursor().peek(n); }
    [[nodiscard]] const token& tok() { return parser().cursor().tok(); }
    [[nodiscard]] token_kind kind_at(std::uint32_t n) { return parser().cursor().kind_at(n); }
    [[nodiscard]] bool at(token_kind k) { return parser().cursor().at(k); }
    token advance() { return parser().cursor().advance(); }
    bool eat(token_kind k) { return parser().cursor().eat(k); }
    token expect(token_kind k) { return parser().cursor().expect(k); }
    [[nodiscard]] bool at_name() { return parser().cursor().at_name(); }
    token expect_name() { return parser().cursor().expect_name(); }
    [[nodiscard]] bool starts_value() { return parser().cursor().starts_value(); }
    [[nodiscard]] std::uint32_t prev_end() { return parser().cursor().prev_end(); }
    [[nodiscard]] std::string_view source() { return parser().cursor().text(); }
    [[nodiscard]] std::string_view text_of(const token& t) { return parser().cursor().text_of(t); }
    [[noreturn]] void fail_here(std::string_view what = "invalid syntax") { parser().cursor().fail_here(what); }

    // ── what is written ──
    [[nodiscard]] row_writer& rows() { return parser().output().rows(); }
    [[nodiscard]] parse_output& out() { return parser().output(); }
    [[nodiscard]] auto& names() { return parser().names(); }
    [[nodiscard]] std::uint32_t intern(const token& t) { return static_cast<std::uint32_t>(names().intern(text_of(t))); }

    // ── the parser's ──
    [[noreturn]] static void fail(std::string message, std::uint32_t offset) { Parser::errors::fail(std::move(message), offset); }
    [[nodiscard]] auto nest() { return parser().nest(); }
    [[nodiscard]] auto save() { return parser().save(); }
    template <class Checkpoint>
    void rewind(const Checkpoint& c) { parser().rewind(c); }

    // ── checks and walks over the rows ──
    template <class Fn>
    void for_each_root(std::uint32_t first, std::uint32_t end, Fn&& fn) { grammar::for_each_root(rows(), first, end, std::forward<Fn>(fn)); }
    [[nodiscard]] std::string_view describe(const expr& e) { return describe_kind(rows()[e.row()]); }
    // Turns an expression into a target in place: context Store or Del, checked as it goes.
    void to_target(const expr& e, std::uint16_t ctx) { make_target<typename Parser::errors>(rows(), e.row(), ctx); }
    // A walrus where the grammar has an expression (a dict key, a slice's bound): there it must be
    // in parentheses.
    void refuse_unparenthesized_walrus(const expr& e) {
        if (rows()[e.row()].kind() == kind::NamedExpr && !e.parenthesized()) fail("invalid syntax", e.start());
    }
};

}  // namespace pygim::code::python::grammar
