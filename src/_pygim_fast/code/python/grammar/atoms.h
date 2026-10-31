#pragma once
// code/python/grammar/atoms.h — atoms: names, numbers, constants, and the displays in brackets —
// groups, tuples, lists, dicts, sets, comprehensions.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "../../../utils/chars.h"
#include "part.h"

namespace pygim::code::python::grammar {

template <class Parser>
class atom_rules : public part_of<Parser, atom_rules<Parser>> {
    using base = part_of<Parser, atom_rules<Parser>>;
    using base::tok;
    using base::at;
    using base::advance;
    using base::eat;
    using base::expect;
    using base::fail_here;
    using base::intern;
    using base::source;
    using base::rows;
    using base::fail;
    using base::nest;
    using base::refuse_unparenthesized_walrus;
    using base::parser;

public:
    /// The most digits a decimal integer literal may have: CPython's default int_max_str_digits,
    /// which ast.parse applies (a Python run with sys.set_int_max_str_digits changed differs).
    static constexpr std::size_t max_str_digits = 4300;

    expr atom() {
        const std::uint32_t first = rows().mark();
        const token t = tok();
        auto constant = [&](constant_kind k) {
            advance();
            return expr(rows().close(kind::Constant, first, t.start(), t.end(), static_cast<std::uint16_t>(k)), first, t.start(), t.end());
        };
        switch (t.kind()) {
            case token_kind::name:
            case token_kind::kw_match:
            case token_kind::kw_case:
            case token_kind::kw_type:
                advance();
                return {rows().close(kind::Name, first, t.start(), t.end(), load, intern(t)), first, t.start(), t.end()};
            case token_kind::number: advance(); return {number_constant(t), first, t.start(), t.end()};
            case token_kind::string:
            case token_kind::fstring_start: return parser().strings();
            case token_kind::kw_None: return constant(constant_kind::none);
            case token_kind::kw_True: return constant(constant_kind::true_);
            case token_kind::kw_False: return constant(constant_kind::false_);
            case token_kind::ellipsis: return constant(constant_kind::ellipsis);
            case token_kind::lpar: return parenthesized();
            case token_kind::lsqb: return list_display();
            case token_kind::lbrace: return dict_or_set_display();
            default: fail_here();
        }
    }

    std::uint32_t number_constant(const token& t) {
        const constant_kind k = t.has(token_flag::imaginary) ? constant_kind::imaginary : t.has(token_flag::floating) ? constant_kind::floating : constant_kind::integer;
        if (k == constant_kind::integer) refuse_too_many_digits(t);
        return rows().close(kind::Constant, rows().mark(), t.start(), t.end(), static_cast<std::uint16_t>(k));
    }

    // `( ... )`: an empty tuple, a yield, a group, a tuple, a generator.
    expr parenthesized() {
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        const token l = advance();
        if (at(token_kind::rpar)) {
            const token r = advance();
            return {rows().close(kind::Tuple, first, l.start(), r.end(), load), first, l.start(), r.end()};
        }
        if (at(token_kind::kw_yield)) {
            const expr y = parser().yield_expression();
            const token r = expect(token_kind::rpar);
            return y.in_parentheses(l.start(), r.end());
        }
        const expr e = parser().star_named_expression();
        if (parser().at_comprehension()) {
            if (rows()[e.row()].kind() == kind::Starred) fail("iterable unpacking cannot be used in comprehension", e.start());
            rows().slot(e.row(), slot::GeneratorExp::elt);
            parser().comprehension_clauses(slot::GeneratorExp::generators);
            const token r = expect(token_kind::rpar);
            return {rows().close(kind::GeneratorExp, first, l.start(), r.end()), first, l.start(), r.end()};
        }
        if (at(token_kind::comma)) {
            rows().slot(e.row(), slot::Tuple::elts);
            while (eat(token_kind::comma) && !at(token_kind::rpar)) rows().slot(parser().star_named_expression().row(), slot::Tuple::elts);
            const token r = expect(token_kind::rpar);
            return {rows().close(kind::Tuple, first, l.start(), r.end(), load), first, l.start(), r.end()};
        }
        const token r = expect(token_kind::rpar);
        if (rows()[e.row()].kind() == kind::Starred) fail("cannot use starred expression here", e.start());   // `((*x))`: refused inside already
        return e.in_parentheses(l.start(), r.end());
    }

    expr list_display() {
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        const token l = advance();
        if (!at(token_kind::rsqb)) {
            const expr e = parser().star_named_expression();
            if (parser().at_comprehension()) {
                if (rows()[e.row()].kind() == kind::Starred) fail("iterable unpacking cannot be used in comprehension", e.start());
                rows().slot(e.row(), slot::ListComp::elt);
                parser().comprehension_clauses(slot::ListComp::generators);
                const token r = expect(token_kind::rsqb);
                return {rows().close(kind::ListComp, first, l.start(), r.end()), first, l.start(), r.end()};
            }
            rows().slot(e.row(), slot::List::elts);
            while (eat(token_kind::comma) && !at(token_kind::rsqb)) rows().slot(parser().star_named_expression().row(), slot::List::elts);
        }
        const token r = expect(token_kind::rsqb);
        return {rows().close(kind::List, first, l.start(), r.end(), load), first, l.start(), r.end()};
    }

    expr dict_or_set_display() {
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        const token l = advance();
        if (at(token_kind::rbrace)) {
            const token r = advance();
            return {rows().close(kind::Dict, first, l.start(), r.end()), first, l.start(), r.end()};
        }
        if (at(token_kind::doublestar)) return dict_display(first, l);
        const expr e = parser().star_named_expression();
        if (at(token_kind::colon)) {
            if (rows()[e.row()].kind() == kind::Starred) fail("cannot use a starred expression in a dictionary value", e.start());
            refuse_unparenthesized_walrus(e);   // kvpair: expression ':' expression
            advance();
            const expr v = parser().expression();
            if (parser().at_comprehension()) {
                rows().slot(e.row(), slot::DictComp::key);
                rows().slot(v.row(), slot::DictComp::value);
                parser().comprehension_clauses(slot::DictComp::generators);
                const token r = expect(token_kind::rbrace);
                return {rows().close(kind::DictComp, first, l.start(), r.end()), first, l.start(), r.end()};
            }
            rows().slot(e.row(), slot::Dict::keys);
            rows().slot(v.row(), slot::Dict::values);
            if (!eat(token_kind::comma)) {
                const token r = expect(token_kind::rbrace);
                return {rows().close(kind::Dict, first, l.start(), r.end()), first, l.start(), r.end()};
            }
            return dict_display(first, l);
        }
        if (parser().at_comprehension()) {
            if (rows()[e.row()].kind() == kind::Starred) fail("iterable unpacking cannot be used in comprehension", e.start());
            rows().slot(e.row(), slot::SetComp::elt);
            parser().comprehension_clauses(slot::SetComp::generators);
            const token r = expect(token_kind::rbrace);
            return {rows().close(kind::SetComp, first, l.start(), r.end()), first, l.start(), r.end()};
        }
        rows().slot(e.row(), slot::Set::elts);
        while (eat(token_kind::comma) && !at(token_kind::rbrace)) rows().slot(parser().star_named_expression().row(), slot::Set::elts);
        const token r = expect(token_kind::rbrace);
        return {rows().close(kind::Set, first, l.start(), r.end()), first, l.start(), r.end()};
    }
    // The rest of a dict display: `k: v` and `**m` items up to `}`.
    expr dict_display(std::uint32_t first, const token& l) {
        while (!at(token_kind::rbrace)) {
            if (eat(token_kind::doublestar)) {
                rows().leaf(kind::absent, slot::Dict::keys, 0, no_value);
                rows().slot(parser().bitwise_or().row(), slot::Dict::values);
            } else {
                rows().slot(parser().expression().row(), slot::Dict::keys);
                expect(token_kind::colon);
                rows().slot(parser().expression().row(), slot::Dict::values);
            }
            if (!eat(token_kind::comma)) break;
        }
        const token r = expect(token_kind::rbrace);
        return {rows().close(kind::Dict, first, l.start(), r.end()), first, l.start(), r.end()};
    }

    // A decimal integer of more digits than CPython converts (its underscores not counted).
    void refuse_too_many_digits(const token& t) {
        const std::string_view digits = source().substr(t.start(), t.end() - t.start());
        if (digits.size() > 1 && digits[0] == '0' && digits[1] != '_' && !chars::is_digit(digits[1])) return;   // 0x, 0o, 0b
        const auto n = static_cast<std::size_t>(std::count_if(digits.begin(), digits.end(), [](char c) { return chars::is_digit(c); }));
        if (n > max_str_digits) {
            fail("Exceeds the limit (4300 digits) for integer string conversion: value has " + std::to_string(n) +
                             " digits; use sys.set_int_max_str_digits() to increase the limit",
                         t.start());
        }
    }
};

}  // namespace pygim::code::python::grammar
