#pragma once
// code/python/grammar/patterns.h — the match statement and its patterns (PEP 634); `match` is a
// name everywhere else.
//
// CORE layer: pybind-free.

#include "part.h"

namespace pygim::code::python::grammar {

template <class Parser>
class pattern_rules : public part_of<Parser, pattern_rules<Parser>> {
    using base = part_of<Parser, pattern_rules<Parser>>;
    using base::tok;
    using base::kind_at;
    using base::at;
    using base::advance;
    using base::eat;
    using base::expect;
    using base::fail_here;
    using base::at_name;
    using base::expect_name;
    using base::prev_end;
    using base::text_of;
    using base::intern;
    using base::rows;
    using base::fail;
    using base::nest;
    using base::save;
    using base::rewind;
    using base::parser;

public:
    // A match statement, if the line is one: `match` is a name everywhere else.
    bool match_statement(std::uint32_t first, std::uint32_t& out) {
        if (!info(kind_at(1)).starts_expression()) return false;
        const auto c = save();
        const token m = advance();
        bool is_match = false;
        try {
            const expr subject = subject_expression();
            rows().slot(subject.row(), slot::Match::subject);
            is_match = at(token_kind::colon) && kind_at(1) == token_kind::newline;
        } catch (const syntax_error&) {
        }
        if (!is_match) {
            rewind(c);
            return false;
        }
        advance();   // ':'
        advance();   // NEWLINE
        expect(token_kind::indent);
        std::uint32_t end = 0;
        do end = case_block();
        while (at(token_kind::kw_case));
        expect(token_kind::dedent);
        out = rows().close(kind::Match, first, m.start(), end);
        return true;
    }

    expr subject_expression() {
        const std::uint32_t first = rows().mark();
        const expr e = parser().star_named_expression();
        if (!at(token_kind::comma)) {
            if (rows()[e.row()].kind() == kind::Starred) fail("can't use starred expression here", e.start());
            return e;
        }
        rows().slot(e.row(), slot::Tuple::elts);
        while (eat(token_kind::comma) && !at(token_kind::colon)) rows().slot(parser().star_named_expression().row(), slot::Tuple::elts);
        return {rows().close(kind::Tuple, first, e.start(), prev_end(), load), first, e.start(), prev_end()};
    }

    std::uint32_t case_block() {
        const std::uint32_t first = rows().mark();
        if (!at(token_kind::kw_case)) fail_here("invalid syntax: expected 'case'");
        advance();
        rows().slot(patterns(), slot::match_case::pattern);
        if (eat(token_kind::kw_if)) rows().slot(parser().named_expression().row(), slot::match_case::guard);
        const std::uint32_t end = parser().suite(slot::match_case::body);
        rows().slot(rows().close(kind::match_case, first, 0, 0), slot::Match::cases);
        return end;
    }

    // The patterns of a case: one, or an open sequence `a, *b`.
    std::uint32_t patterns() {
        const std::uint32_t first = rows().mark();
        const std::uint32_t start = tok().start();
        const std::uint32_t p = maybe_star_pattern();
        if (!at(token_kind::comma)) {
            if (rows()[p].kind() == kind::MatchStar) fail("can't use starred pattern here", start);
            return p;
        }
        rows().slot(p, slot::MatchSequence::patterns);
        while (eat(token_kind::comma) && !at(token_kind::colon) && !at(token_kind::kw_if)) rows().slot(maybe_star_pattern(), slot::MatchSequence::patterns);
        return rows().close(kind::MatchSequence, first, start, prev_end());
    }

    std::uint32_t maybe_star_pattern() {
        if (!at(token_kind::star)) return pattern();
        const std::uint32_t first = rows().mark();
        const token s = advance();
        const token n = expect_name();
        return rows().close(kind::MatchStar, first, s.start(), n.end(), 0, text_of(n) == "_" ? no_value : intern(n));
    }

    // `or_pattern ['as' NAME]`
    std::uint32_t pattern() {
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        const std::uint32_t start = tok().start();
        const std::uint32_t p = or_pattern();
        if (!eat(token_kind::kw_as)) return p;
        const token n = expect_name();
        if (text_of(n) == "_") fail("cannot use '_' as a target", n.start());
        rows().slot(p, slot::MatchAs::pattern);
        return rows().close(kind::MatchAs, first, start, n.end(), 0, intern(n));
    }

    std::uint32_t or_pattern() {
        const std::uint32_t first = rows().mark();
        const std::uint32_t start = tok().start();
        const std::uint32_t p = closed_pattern();
        if (!at(token_kind::vbar)) return p;
        rows().slot(p, slot::MatchOr::patterns);
        while (eat(token_kind::vbar)) rows().slot(closed_pattern(), slot::MatchOr::patterns);
        return rows().close(kind::MatchOr, first, start, prev_end());
    }

    std::uint32_t closed_pattern() {
        const std::uint32_t first = rows().mark();
        const token t = tok();
        switch (t.kind()) {
            case token_kind::number:
            case token_kind::minus: {
                const expr v = signed_number_pattern();
                rows().slot(v.row(), slot::MatchValue::value);
                return rows().close(kind::MatchValue, first, v.start(), v.end());
            }
            case token_kind::string:
            case token_kind::fstring_start: {   // an f-string too: ast.parse takes it, the compiler refuses it later
                const expr v = parser().strings();
                rows().slot(v.row(), slot::MatchValue::value);
                return rows().close(kind::MatchValue, first, v.start(), v.end());
            }
            case token_kind::kw_None: advance(); return rows().close(kind::MatchSingleton, first, t.start(), t.end(), static_cast<std::uint16_t>(constant_kind::none));
            case token_kind::kw_True: advance(); return rows().close(kind::MatchSingleton, first, t.start(), t.end(), static_cast<std::uint16_t>(constant_kind::true_));
            case token_kind::kw_False: advance(); return rows().close(kind::MatchSingleton, first, t.start(), t.end(), static_cast<std::uint16_t>(constant_kind::false_));
            case token_kind::lpar: return group_or_sequence_pattern(first);
            case token_kind::lsqb: {
                advance();
                while (!at(token_kind::rsqb)) {
                    rows().slot(maybe_star_pattern(), slot::MatchSequence::patterns);
                    if (!eat(token_kind::comma)) break;
                }
                const token r = expect(token_kind::rsqb);
                return rows().close(kind::MatchSequence, first, t.start(), r.end());
            }
            case token_kind::lbrace: return mapping_pattern(first);
            default: break;
        }
        if (!at_name()) fail_here("invalid syntax: expected a pattern");
        if (kind_at(1) != token_kind::dot && kind_at(1) != token_kind::lpar) {   // a capture, or the wildcard
            advance();
            if (text_of(t) == "_") return rows().close(kind::MatchAs, first, t.start(), t.end());
            return rows().close(kind::MatchAs, first, t.start(), t.end(), 0, intern(t));
        }
        const expr cls = name_or_attribute();
        if (at(token_kind::lpar)) return class_pattern(first, cls);
        rows().slot(cls.row(), slot::MatchValue::value);
        return rows().close(kind::MatchValue, first, cls.start(), cls.end());
    }

    // `a.b.c` as Name and Attribute rows, loaded.
    expr name_or_attribute() {
        const std::uint32_t first = rows().mark();
        const token n = advance();
        expr e(rows().close(kind::Name, first, n.start(), n.end(), load, intern(n)), first, n.start(), n.end());
        while (eat(token_kind::dot)) {
            const token a = expect_name();
            rows().slot(e.row(), slot::Attribute::value);
            e = expr(rows().close(kind::Attribute, first, e.start(), a.end(), load, intern(a)), first, e.start(), a.end());
        }
        return e;
    }

    // A number in a pattern: `1`, `-1`, `1 + 2j`, `-1.5 - 3j`.
    expr signed_number_pattern() {
        const std::uint32_t first = rows().mark();
        const std::uint32_t start = tok().start();
        expr real = signed_number();
        if (!at(token_kind::plus) && !at(token_kind::minus)) return real;
        if (rows()[real.row()].kind() == kind::Constant && constant_of(rows()[real.row()].small()) == constant_kind::imaginary) {
            fail("real number required in complex literal", real.start());
        }
        const std::uint16_t op = at(token_kind::plus) ? detail::binary_index(kind::Add) : detail::binary_index(kind::Sub);
        advance();
        const token im = tok();
        if (im.kind() != token_kind::number || !im.has(token_flag::imaginary)) fail_here("imaginary number required in complex literal");
        advance();
        rows().slot(real.row(), slot::BinOp::left);
        rows().slot(rows().close(kind::Constant, rows().mark(), im.start(), im.end(), static_cast<std::uint16_t>(constant_kind::imaginary)), slot::BinOp::right);
        return {rows().close(kind::BinOp, first, start, im.end(), op), first, start, im.end()};
    }
    expr signed_number() {
        const std::uint32_t first = rows().mark();
        const token t = tok();
        if (eat(token_kind::minus)) {
            const token n = expect(token_kind::number);
            rows().slot(parser().number_constant(n), slot::UnaryOp::operand);
            return {rows().close(kind::UnaryOp, first, t.start(), n.end(), unary_usub), first, t.start(), n.end()};
        }
        const token n = expect(token_kind::number);
        return {parser().number_constant(n), first, n.start(), n.end()};
    }

    std::uint32_t group_or_sequence_pattern(std::uint32_t first) {
        const token l = advance();
        if (at(token_kind::rpar)) {
            const token r = advance();
            return rows().close(kind::MatchSequence, first, l.start(), r.end());
        }
        const std::uint32_t p = maybe_star_pattern();
        if (at(token_kind::rpar)) {   // a group: the pattern itself; `(*x)` is no pattern
            if (rows()[p].kind() == kind::MatchStar) fail_here();
            advance();
            return p;
        }
        rows().slot(p, slot::MatchSequence::patterns);
        while (eat(token_kind::comma) && !at(token_kind::rpar)) rows().slot(maybe_star_pattern(), slot::MatchSequence::patterns);
        const token r = expect(token_kind::rpar);
        return rows().close(kind::MatchSequence, first, l.start(), r.end());
    }

    std::uint32_t mapping_pattern(std::uint32_t first) {
        const token l = advance();
        std::uint32_t rest = no_value;
        while (!at(token_kind::rbrace)) {
            if (eat(token_kind::doublestar)) {
                const token n = expect_name();
                if (text_of(n) == "_") fail("invalid syntax: '**_' is not a mapping pattern's rest", n.start());
                rest = intern(n);
                eat(token_kind::comma);
                break;
            }
            rows().slot(mapping_key().row(), slot::MatchMapping::keys);
            expect(token_kind::colon);
            rows().slot(pattern(), slot::MatchMapping::patterns);
            if (!eat(token_kind::comma)) break;
        }
        const token r = expect(token_kind::rbrace);
        return rows().close(kind::MatchMapping, first, l.start(), r.end(), 0, rest);
    }
    // A mapping pattern's key: a literal or a dotted name.
    expr mapping_key() {
        const std::uint32_t first = rows().mark();
        const token t = tok();
        switch (t.kind()) {
            case token_kind::number:
            case token_kind::minus: return signed_number_pattern();
            case token_kind::string:
            case token_kind::fstring_start: return parser().strings();   // as a value pattern's
            case token_kind::kw_None: advance(); return {rows().close(kind::Constant, first, t.start(), t.end(), static_cast<std::uint16_t>(constant_kind::none)), first, t.start(), t.end()};
            case token_kind::kw_True: advance(); return {rows().close(kind::Constant, first, t.start(), t.end(), static_cast<std::uint16_t>(constant_kind::true_)), first, t.start(), t.end()};
            case token_kind::kw_False: advance(); return {rows().close(kind::Constant, first, t.start(), t.end(), static_cast<std::uint16_t>(constant_kind::false_)), first, t.start(), t.end()};
            default: break;
        }
        if (!at_name() || kind_at(1) != token_kind::dot) fail_here("mapping pattern keys may only match literals and attribute lookups");
        return name_or_attribute();
    }

    std::uint32_t class_pattern(std::uint32_t first, const expr& cls) {
        rows().slot(cls.row(), slot::MatchClass::cls);
        advance();   // `(`
        bool keywords = false;
        while (!at(token_kind::rpar)) {
            if (at_name() && kind_at(1) == token_kind::equal) {
                const token n = advance();
                advance();
                rows().leaf(kind::identifier, slot::MatchClass::kwd_attrs, 0, intern(n));
                rows().slot(pattern(), slot::MatchClass::kwd_patterns);
                keywords = true;
            } else {
                if (keywords) fail_here("positional patterns follow keyword patterns");
                rows().slot(pattern(), slot::MatchClass::patterns);
            }
            if (!eat(token_kind::comma)) break;
        }
        const token r = expect(token_kind::rpar);
        return rows().close(kind::MatchClass, first, cls.start(), r.end());
    }
};

}  // namespace pygim::code::python::grammar
