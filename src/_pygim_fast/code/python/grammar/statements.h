#pragma once
// code/python/grammar/statements.h — simple statements, a line of them, and blocks: what a
// statement is, read by its first token.
//
// CORE layer: pybind-free.

#include <cstdint>
#include <string>

#include "part.h"

namespace pygim::code::python::grammar {

template <class Parser>
class statement_rules : public part_of<Parser, statement_rules<Parser>> {
    using base = part_of<Parser, statement_rules<Parser>>;
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
    using base::starts_value;
    using base::text_of;
    using base::intern;
    using base::names;
    using base::rows;
    using base::fail;
    using base::nest;
    using base::describe;
    using base::to_target;
    using base::parser;

public:
    /// The most packages a relative import may climb: what a row's 16-bit field holds.
    static constexpr std::uint32_t max_import_level = 65'535;

    // One statement line or compound statement, each statement in `field`; where it ends (a line's
    // end takes in a `;` after its last statement, as CPython's block does).
    std::uint32_t statement(std::uint8_t field) {
        const auto guard = nest();
        const std::uint32_t first = rows().mark();
        std::uint32_t r = 0;
        switch (tok().kind()) {
            case token_kind::kw_if: r = parser().if_statement(first); break;
            case token_kind::kw_while: r = parser().while_statement(first); break;
            case token_kind::kw_for: r = parser().for_statement(first, tok().start(), false); break;
            case token_kind::kw_try: r = parser().try_statement(first); break;
            case token_kind::kw_with: r = parser().with_statement(first, tok().start(), false); break;
            case token_kind::kw_def: r = parser().function_def(first, tok().start(), false); break;
            case token_kind::kw_class: r = parser().class_def(first, tok().start()); break;
            case token_kind::at: r = parser().decorated(first); break;
            case token_kind::kw_async: r = parser().async_statement(first); break;
            case token_kind::kw_match:
                if (std::uint32_t m = 0; parser().match_statement(first, m)) {
                    r = m;
                    break;
                }
                return simple_statements(field);
            case token_kind::indent: fail_here("unexpected indent");
            case token_kind::dedent: fail_here("unindent does not match any outer indentation level");
            default: return simple_statements(field);
        }
        rows().slot(r, field);
        return rows()[r].end();
    }

    // `a; b; c` up to the end of the line, each in `field`; where the line's last token ends.
    std::uint32_t simple_statements(std::uint8_t field) {
        for (;;) {
            rows().slot(simple_statement(), field);
            if (!eat(token_kind::semi) || at(token_kind::newline)) break;
        }
        const std::uint32_t end = prev_end();
        expect(token_kind::newline);
        return end;
    }

    // A block: an indented suite or the rest of the line; where its last statement ends.
    std::uint32_t block(std::uint8_t field) {
        if (!eat(token_kind::newline)) return simple_statements(field);
        expect(token_kind::indent);
        std::uint32_t end = 0;
        while (!at(token_kind::dedent) && !at(token_kind::end_of_file)) end = statement(field);
        expect(token_kind::dedent);
        return end;
    }
    // `:` then a block.
    std::uint32_t suite(std::uint8_t field) {
        expect(token_kind::colon);
        return block(field);
    }

    std::uint32_t simple_statement() {
        const std::uint32_t first = rows().mark();
        const token t = tok();
        switch (t.kind()) {
            case token_kind::kw_pass: advance(); return rows().close(kind::Pass, first, t.start(), t.end());
            case token_kind::kw_break: advance(); return rows().close(kind::Break, first, t.start(), t.end());
            case token_kind::kw_continue: advance(); return rows().close(kind::Continue, first, t.start(), t.end());
            case token_kind::kw_return: {
                advance();
                if (starts_value()) rows().slot(parser().star_expressions().row(), slot::Return::value);
                return rows().close(kind::Return, first, t.start(), prev_end());
            }
            case token_kind::kw_raise: {
                advance();
                if (starts_value()) {
                    rows().slot(parser().expression().row(), slot::Raise::exc);
                    if (eat(token_kind::kw_from)) rows().slot(parser().expression().row(), slot::Raise::cause);
                }
                return rows().close(kind::Raise, first, t.start(), prev_end());
            }
            case token_kind::kw_global:
            case token_kind::kw_nonlocal: {
                advance();
                const bool global = t.kind() == token_kind::kw_global;
                do {
                    const token n = expect_name();
                    rows().leaf(kind::identifier, global ? slot::Global::names : slot::Nonlocal::names, 0, intern(n));
                } while (eat(token_kind::comma));
                return rows().close(global ? kind::Global : kind::Nonlocal, first, t.start(), prev_end());
            }
            case token_kind::kw_del: return del_statement(first);
            case token_kind::kw_assert: {
                advance();
                rows().slot(parser().expression().row(), slot::Assert::test);
                if (eat(token_kind::comma)) rows().slot(parser().expression().row(), slot::Assert::msg);
                return rows().close(kind::Assert, first, t.start(), prev_end());
            }
            case token_kind::kw_import: return import_name(first);
            case token_kind::kw_from: return import_from(first);
            case token_kind::kw_type:
                if (info(kind_at(1)).name_like()) return type_alias(first);
                break;
            default: break;
        }
        return expression_statement(first);
    }


    std::uint32_t expression_statement(std::uint32_t first) {
        const std::uint32_t start = tok().start();
        expr e = parser().annotated_rhs();
        const token_kind next = tok().kind();
        if (next == token_kind::equal) {
            while (eat(token_kind::equal)) {
                to_target(e, 1);
                rows().slot(e.row(), slot::Assign::targets);
                e = parser().annotated_rhs();
            }
            rows().slot(e.row(), slot::Assign::value);
            return rows().close(kind::Assign, first, start, e.end());
        }
        if (next == token_kind::colon) return annotated_assignment(first, start, e);
        if (const std::uint8_t op = info(next).aug_op(); op != 0) {
            const enum kind k = rows()[e.row()].kind();
            if (k != kind::Name && k != kind::Attribute && k != kind::Subscript) {
                fail("'" + std::string(describe(e)) + "' is an illegal expression for augmented assignment", e.start());
            }
            to_target(e, 1);
            rows().slot(e.row(), slot::AugAssign::target);
            advance();
            const expr v = parser().annotated_rhs();
            rows().slot(v.row(), slot::AugAssign::value);
            return rows().close(kind::AugAssign, first, start, v.end(), static_cast<std::uint16_t>(op - 1));
        }
        rows().slot(e.row(), slot::Expr::value);
        return rows().close(kind::Expr, first, start, e.end());
    }

    std::uint32_t annotated_assignment(std::uint32_t first, std::uint32_t start, const expr& target) {
        const enum kind k = rows()[target.row()].kind();
        if (k == kind::List || k == kind::Tuple) fail(std::string("only single target (not ") + (k == kind::List ? "list" : "tuple") + ") can be annotated", target.start());
        if (k != kind::Name && k != kind::Attribute && k != kind::Subscript) fail("illegal target for annotation", target.start());
        const std::uint16_t simple = (k == kind::Name && !target.parenthesized()) ? 1 : 0;
        to_target(target, 1);
        rows().slot(target.row(), slot::AnnAssign::target);
        advance();   // ':'
        rows().slot(parser().expression().row(), slot::AnnAssign::annotation);
        if (eat(token_kind::equal)) rows().slot(parser().annotated_rhs().row(), slot::AnnAssign::value);
        return rows().close(kind::AnnAssign, first, start, prev_end(), simple);
    }

    std::uint32_t del_statement(std::uint32_t first) {
        const token t = advance();
        do {
            const expr e = parser().bitwise_or();
            to_target(e, 2);
            rows().slot(e.row(), slot::Delete::targets);
        } while (eat(token_kind::comma) && starts_value());
        return rows().close(kind::Delete, first, t.start(), prev_end());
    }

    // `a.b.c` as one identifier: its id, start and end.
    std::uint32_t dotted_name() {
        const token head = expect_name();
        if (!at(token_kind::dot)) return intern(head);
        m_scratch.assign(text_of(head));
        while (eat(token_kind::dot)) {
            m_scratch.push_back('.');
            m_scratch.append(text_of(expect_name()));
        }
        return static_cast<std::uint32_t>(names().intern(m_scratch));
    }

    std::uint32_t import_name(std::uint32_t first) {
        const token t = advance();
        do {
            const std::uint32_t a = rows().mark();
            const std::uint32_t start = tok().start();
            const std::uint32_t name = dotted_name();
            if (eat(token_kind::kw_as)) rows().leaf(kind::identifier, slot::alias::asname, 0, intern(expect_name()));
            rows().slot(rows().close(kind::alias, a, start, prev_end(), 0, name), slot::Import::names);
        } while (eat(token_kind::comma));
        return rows().close(kind::Import, first, t.start(), prev_end());
    }

    std::uint32_t import_from(std::uint32_t first) {
        const token t = advance();
        std::uint32_t level = 0;
        for (;;) {
            if (eat(token_kind::dot)) ++level;
            else if (eat(token_kind::ellipsis)) level += 3;
            else break;
        }
        // a row holds the level in 16 bits; CPython takes more, which no import has needed (code_parser.md)
        if (level > max_import_level) fail("a relative import more than 65,535 packages up is not read", t.start());
        std::uint32_t module = no_value;
        if (at_name()) module = dotted_name();
        else if (level == 0) fail_here("invalid syntax: expected a module name");
        expect(token_kind::kw_import);
        if (at(token_kind::star)) {
            const token s = advance();
            const std::uint32_t a = rows().mark();
            rows().slot(rows().close(kind::alias, a, s.start(), s.end(), 0, static_cast<std::uint32_t>(names().intern("*"))), slot::ImportFrom::names);
        } else {
            const bool parens = eat(token_kind::lpar);
            for (;;) {
                const std::uint32_t a = rows().mark();
                const token n = expect_name();
                if (eat(token_kind::kw_as)) rows().leaf(kind::identifier, slot::alias::asname, 0, intern(expect_name()));
                rows().slot(rows().close(kind::alias, a, n.start(), prev_end(), 0, intern(n)), slot::ImportFrom::names);
                if (!eat(token_kind::comma)) break;
                if (!parens && !at_name()) fail_here("trailing comma not allowed without surrounding parentheses");
                if (parens && at(token_kind::rpar)) break;
            }
            if (parens) expect(token_kind::rpar);
        }
        return rows().close(kind::ImportFrom, first, t.start(), prev_end(), static_cast<std::uint16_t>(level), module);
    }

    std::uint32_t type_alias(std::uint32_t first) {
        const token t = advance();
        const token n = advance();
        const std::uint32_t name_row = rows().close(kind::Name, rows().mark(), n.start(), n.end(), store, intern(n));
        rows().slot(name_row, slot::TypeAlias::name);
        if (at(token_kind::lsqb)) parser().type_params(slot::TypeAlias::type_params);
        expect(token_kind::equal);
        const expr v = parser().expression();
        rows().slot(v.row(), slot::TypeAlias::value);
        return rows().close(kind::TypeAlias, first, t.start(), v.end());
    }

private:
    std::string m_scratch;   // a dotted name being joined
};

}  // namespace pygim::code::python::grammar
