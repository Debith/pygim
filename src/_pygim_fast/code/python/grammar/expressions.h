#pragma once
// code/python/grammar/expressions.h — expressions by operator strength (Pratt), calls,
// subscripts, comprehensions, and target lists.
//
// CORE layer: pybind-free.

#include "part.h"

namespace pygim::code::python::grammar {

template <class Parser>
class expression_rules : public part_of<Parser, expression_rules<Parser>> {
    using base = part_of<Parser, expression_rules<Parser>>;
    using base::tok;
    using base::kind_at;
    using base::at;
    using base::advance;
    using base::eat;
    using base::expect;
    using base::at_name;
    using base::expect_name;
    using base::prev_end;
    using base::starts_value;
    using base::intern;
    using base::rows;
    using base::fail;
    using base::nest;
    using base::describe;
    using base::to_target;
    using base::refuse_unparenthesized_walrus;
    using base::parser;

public:
    // `a, (b, c), *d` up to `in` (a for loop, a comprehension): a target, Store.
    expr target_list() {
        const std::uint32_t first = rows().mark();
        const expr e = target_element();
        if (!at(token_kind::comma)) {
            to_target(e, store);
            return e;
        }
        rows().slot(e.row(), slot::Tuple::elts);
        while (eat(token_kind::comma) && (starts_value() && !at(token_kind::kw_in))) rows().slot(target_element().row(), slot::Tuple::elts);
        const expr t(rows().close(kind::Tuple, first, e.start(), prev_end(), load), first, e.start(), prev_end());
        to_target(t, store);
        return t;
    }
    expr target_element() { return at(token_kind::star) ? starred() : bitwise_or(); }
    // The target of `with ... as`: one target, a tuple only in brackets.
    expr single_target() {
        const expr e = target_element();
        to_target(e, store);
        return e;
    }

    // `a, *b, c` — a tuple when there is a comma (trailing allowed).
    expr star_expressions() {
        const std::uint32_t first = rows().mark();
        const expr e = star_expression();
        if (!at(token_kind::comma)) return e;
        rows().slot(e.row(), slot::Tuple::elts);
        while (eat(token_kind::comma) && starts_value() && !at(token_kind::kw_yield)) rows().slot(star_expression().row(), slot::Tuple::elts);
        return {rows().close(kind::Tuple, first, e.start(), prev_end(), load), first, e.start(), prev_end()};
    }
    expr star_expression() { return at(token_kind::star) ? starred() : expression(); }
    // `*value` at a `*`: a Starred whose value is a bitwise_or (`*a or b` is no starred expression).
    expr starred() {
        const std::uint32_t first = rows().mark();
        const token s = advance();
        const expr v = bitwise_or();
        rows().slot(v.row(), slot::Starred::value);
        return {rows().close(kind::Starred, first, s.start(), v.end(), load), first, s.start(), v.end()};
    }
    expr star_named_expression() {
        if (!at(token_kind::star)) return named_expression();
        return star_expression();
    }
    // What an assignment's or annotation's value is: a yield, or one or more starred expressions
    // (CPython's annotated_rhs).
    expr annotated_rhs() { return at(token_kind::kw_yield) ? yield_expression() : star_expressions(); }
    // `x := value`, or an expression.
    expr named_expression() {
        if (at_name() && kind_at(1) == token_kind::colonequal) {
            const std::uint32_t first = rows().mark();
            const token n = advance();
            advance();
            rows().slot(rows().close(kind::Name, first, n.start(), n.end(), store, intern(n)), slot::NamedExpr::target);
            const expr v = expression();
            rows().slot(v.row(), slot::NamedExpr::value);
            return {rows().close(kind::NamedExpr, first, n.start(), v.end()), first, n.start(), v.end()};
        }
        const expr e = expression();
        if (at(token_kind::colonequal)) fail("cannot use assignment expressions with " + std::string(describe(e)), e.start());
        return e;
    }

    expr expression() {
        const auto guard = nest();
        if (at(token_kind::kw_lambda)) return lambda();
        const expr body = disjunction();
        if (!at(token_kind::kw_if)) return body;
        advance();
        const expr test = disjunction();
        expect(token_kind::kw_else);
        const expr orelse = expression();
        rows().slot(body.row(), slot::IfExp::body);
        rows().slot(test.row(), slot::IfExp::test);
        rows().slot(orelse.row(), slot::IfExp::orelse);
        return {rows().close(kind::IfExp, body.first(), body.start(), orelse.end()), body.first(), body.start(), orelse.end()};
    }

    expr lambda() {
        const std::uint32_t first = rows().mark();
        const token l = advance();
        rows().slot(parser().parameters(false, token_kind::colon), slot::Lambda::args);
        expect(token_kind::colon);
        const expr body = expression();
        rows().slot(body.row(), slot::Lambda::body);
        return {rows().close(kind::Lambda, first, l.start(), body.end()), first, l.start(), body.end()};
    }

    // `a or b or c`, `a and b`: one BoolOp of every operand.
    using rule = expr (expression_rules::*)();   // a rule of this part, called on it
    expr disjunction() { return bool_operation(token_kind::kw_or, member_index(kind::Or_, family::boolop), &expression_rules::conjunction); }
    expr conjunction() { return bool_operation(token_kind::kw_and, member_index(kind::And_, family::boolop), &expression_rules::inversion); }
    expr bool_operation(token_kind op, std::uint16_t which, rule operand) {
        const expr first_operand = (this->*operand)();
        if (!at(op)) return first_operand;
        rows().slot(first_operand.row(), slot::BoolOp::values);
        expr last = first_operand;
        while (eat(op)) {
            last = (this->*operand)();
            rows().slot(last.row(), slot::BoolOp::values);
        }
        const std::uint32_t f = first_operand.first();
        return {rows().close(kind::BoolOp, f, first_operand.start(), last.end(), which), f, first_operand.start(), last.end()};
    }
    expr inversion() {
        if (!at(token_kind::kw_not)) return comparison();
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        const token n = advance();
        const expr v = inversion();
        rows().slot(v.row(), slot::UnaryOp::operand);
        return {rows().close(kind::UnaryOp, first, n.start(), v.end(), unary_not), first, n.start(), v.end()};
    }

    // `a < b < c`: one Compare, its operators as leaf rows between the operands.
    expr comparison() {
        const expr left = bitwise_or();
        std::uint8_t op = compare_here();
        if (op == 0) return left;
        rows().slot(left.row(), slot::Compare::left);
        expr last = left;
        while (op != 0) {
            rows().leaf(kind::cmpop, slot::Compare::ops, static_cast<std::uint16_t>(op - 1), no_value);
            last = bitwise_or();
            rows().slot(last.row(), slot::Compare::comparators);
            op = compare_here();
        }
        return {rows().close(kind::Compare, left.first(), left.start(), last.end()), left.first(), left.start(), last.end()};
    }
    // The comparison operator here, taken (its cmpop index + 1), or 0.
    std::uint8_t compare_here() {
        const token_kind k = tok().kind();
        if (k == token_kind::kw_not) {
            if (kind_at(1) != token_kind::kw_in) return 0;
            advance();
            advance();
            return detail::compare_index(kind::NotIn) + 1;
        }
        if (k == token_kind::kw_is) {
            advance();
            if (eat(token_kind::kw_not)) return detail::compare_index(kind::IsNot) + 1;
            return detail::compare_index(kind::Is_) + 1;
        }
        const std::uint8_t op = info(k).compare_op();
        if (op != 0) advance();
        return op;
    }

    // `|` down to `* / // % @`: Pratt, by each operator's strength (left-associative).
    expr bitwise_or() { return binary(1); }
    expr binary(std::uint8_t min_power) {
        expr left = factor();
        for (;;) {
            const token_info& op = info(tok().kind());
            if (op.binary_power() < min_power) return left;   // no operator has power 0, below every min_power (≥ 1)
            advance();
            const expr right = binary(static_cast<std::uint8_t>(op.binary_power() + 1));
            rows().slot(left.row(), slot::BinOp::left);
            rows().slot(right.row(), slot::BinOp::right);
            left = expr(rows().close(kind::BinOp, left.first(), left.start(), right.end(), op.binary_op()), left.first(), left.start(), right.end());
        }
    }
    // `+x`, `-x`, `~x`, then `**`.
    expr factor() {
        const token t = tok();
        std::uint16_t op = 0;
        switch (t.kind()) {
            case token_kind::plus: op = unary_uadd; break;
            case token_kind::minus: op = unary_usub; break;
            case token_kind::tilde: op = unary_invert; break;
            default: return power();
        }
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        advance();
        const expr v = factor();
        rows().slot(v.row(), slot::UnaryOp::operand);
        return {rows().close(kind::UnaryOp, first, t.start(), v.end(), op), first, t.start(), v.end()};
    }
    // `a ** b`: right-associative, its exponent a factor (`2 ** -1`).
    expr power() {
        const expr base = await_primary();
        if (!at(token_kind::doublestar)) return base;
        const auto guard = nest();   // `a ** b ** c ...` nests to the right
        advance();
        const expr exponent = factor();
        rows().slot(base.row(), slot::BinOp::left);
        rows().slot(exponent.row(), slot::BinOp::right);
        return {rows().close(kind::BinOp, base.first(), base.start(), exponent.end(), detail::binary_index(kind::Pow)), base.first(), base.start(), exponent.end()};
    }
    expr await_primary() {
        if (!at(token_kind::kw_await)) return primary();
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        const token a = advance();
        const expr v = primary();
        rows().slot(v.row(), slot::Await::value);
        return {rows().close(kind::Await, first, a.start(), v.end()), first, a.start(), v.end()};
    }

    // An atom, then `.name`, `(...)` and `[...]` as they come.
    expr primary() {
        expr e = parser().atom();
        for (;;) {
            if (at(token_kind::dot)) {
                advance();
                const token n = expect_name();
                rows().slot(e.row(), slot::Attribute::value);
                e = expr(rows().close(kind::Attribute, e.first(), e.start(), n.end(), load, intern(n)), e.first(), e.start(), n.end());
            } else if (at(token_kind::lpar)) {
                const token l = advance();
                rows().slot(e.row(), slot::Call::func);
                arguments(slot::Call::args, slot::Call::keywords, l.start(), true);
                e = expr(rows().close(kind::Call, e.first(), e.start(), prev_end()), e.first(), e.start(), prev_end());
            } else if (at(token_kind::lsqb)) {
                advance();
                rows().slot(e.row(), slot::Subscript::value);
                rows().slot(slices().row(), slot::Subscript::slice);
                const token r = expect(token_kind::rsqb);
                e = expr(rows().close(kind::Subscript, e.first(), e.start(), r.end(), load), e.first(), e.start(), r.end());
            } else {
                return e;
            }
        }
    }

    // A call's arguments after its `(`, up to and with its `)`: positional ones in `args`, keyword
    // ones in `keywords`. A lone generator `f(x for x in y)` spans the parentheses.
    void arguments(std::uint8_t args, std::uint8_t keywords, std::uint32_t lpar_start, bool generator_allowed) {
        bool keyword_seen = false, double_star_seen = false;
        std::uint32_t count = 0;
        while (!at(token_kind::rpar)) {
            const std::uint32_t first = rows().mark();
            const token t = tok();
            if (eat(token_kind::star)) {
                if (double_star_seen) fail("iterable argument unpacking follows keyword argument unpacking", t.start());
                const expr v = expression();
                rows().slot(v.row(), slot::Starred::value);
                rows().slot(rows().close(kind::Starred, first, t.start(), v.end(), load), args);
            } else if (eat(token_kind::doublestar)) {
                const expr v = expression();
                rows().slot(v.row(), slot::keyword::value);
                rows().slot(rows().close(kind::keyword, first, t.start(), v.end()), keywords);
                double_star_seen = true;
            } else if (at_name() && kind_at(1) == token_kind::equal) {
                advance();
                advance();
                const expr v = expression();
                rows().slot(v.row(), slot::keyword::value);
                rows().slot(rows().close(kind::keyword, first, t.start(), v.end(), 0, intern(t)), keywords);
                keyword_seen = true;
            } else {
                const expr e = named_expression();
                if (at_comprehension()) {
                    if (!generator_allowed || count != 0) fail("Generator expression must be parenthesized", e.start());
                    rows().slot(e.row(), slot::GeneratorExp::elt);
                    comprehension_clauses(slot::GeneratorExp::generators);
                    if (!at(token_kind::rpar)) fail("Generator expression must be parenthesized", e.start());
                    const token r = tok();
                    rows().slot(rows().close(kind::GeneratorExp, first, lpar_start, r.end()), args);
                } else {
                    if (double_star_seen) fail("positional argument follows keyword argument unpacking", e.start());
                    if (keyword_seen) fail("positional argument follows keyword argument", e.start());
                    rows().slot(e.row(), args);
                }
            }
            ++count;
            if (!eat(token_kind::comma)) break;
        }
        expect(token_kind::rpar);
    }

    // The inside of `[...]` after a primary: one slice or index, or a tuple of them.
    expr slices() {
        const std::uint32_t first = rows().mark();
        const expr s = slice_item();
        if (!at(token_kind::comma) && rows()[s.row()].kind() != kind::Starred) return s;
        rows().slot(s.row(), slot::Tuple::elts);
        while (eat(token_kind::comma) && !at(token_kind::rsqb)) rows().slot(slice_item().row(), slot::Tuple::elts);
        return {rows().close(kind::Tuple, first, s.start(), prev_end(), load), first, s.start(), prev_end()};
    }
    expr slice_item() {
        if (at(token_kind::star)) return star_expression();
        const std::uint32_t first = rows().mark();
        const std::uint32_t start = tok().start();
        if (!at(token_kind::colon)) {
            const expr lower = named_expression();
            if (!at(token_kind::colon)) return lower;
            refuse_unparenthesized_walrus(lower);   // slice: [expression] ':' ..., a named_expression only alone
            rows().slot(lower.row(), slot::Slice::lower);
        }
        advance();   // the first `:`
        if (!at(token_kind::colon) && !at(token_kind::comma) && !at(token_kind::rsqb)) rows().slot(expression().row(), slot::Slice::upper);
        if (eat(token_kind::colon) && !at(token_kind::comma) && !at(token_kind::rsqb)) rows().slot(expression().row(), slot::Slice::step);
        return {rows().close(kind::Slice, first, start, prev_end()), first, start, prev_end()};
    }

    // `for x in y if c` clauses, repeated: comprehension rows in `field`.
    void comprehension_clauses(std::uint8_t field) {
        while (at_comprehension()) {
            const std::uint32_t first = rows().mark();
            const bool is_async = eat(token_kind::kw_async);
            advance();   // `for`
            rows().slot(target_list().row(), slot::comprehension::target);
            expect(token_kind::kw_in);
            rows().slot(disjunction().row(), slot::comprehension::iter);
            while (eat(token_kind::kw_if)) rows().slot(disjunction().row(), slot::comprehension::ifs);
            rows().slot(rows().close(kind::comprehension, first, 0, 0, is_async ? 1 : 0), field);
        }
    }
    [[nodiscard]] bool at_comprehension() { return at(token_kind::kw_for) || (at(token_kind::kw_async) && kind_at(1) == token_kind::kw_for); }

    expr yield_expression() {
        const std::uint32_t first = rows().mark();
        const token y = advance();
        if (eat(token_kind::kw_from)) {
            const expr v = expression();
            rows().slot(v.row(), slot::YieldFrom::value);
            return {rows().close(kind::YieldFrom, first, y.start(), v.end()), first, y.start(), v.end()};
        }
        if (starts_value()) {
            const expr v = star_expressions();
            rows().slot(v.row(), slot::Yield::value);
            return {rows().close(kind::Yield, first, y.start(), v.end()), first, y.start(), v.end()};
        }
        return {rows().close(kind::Yield, first, y.start(), y.end()), first, y.start(), y.end()};
    }
};

}  // namespace pygim::code::python::grammar
