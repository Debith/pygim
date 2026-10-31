#pragma once
// code/python/grammar/compound.h — compound statements: if, while, for, with, try, def, class,
// decorators, parameters.
//
// CORE layer: pybind-free.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "part.h"

namespace pygim::code::python::grammar {

template <class Parser>
class compound_rules : public part_of<Parser, compound_rules<Parser>> {
    using base = part_of<Parser, compound_rules<Parser>>;
    using base::tok;
    using base::kind_at;
    using base::at;
    using base::advance;
    using base::eat;
    using base::expect;
    using base::fail_here;
    using base::expect_name;
    using base::prev_end;
    using base::intern;
    using base::rows;
    using base::fail;
    using base::save;
    using base::rewind;
    using base::for_each_root;
    using base::parser;

public:
    // `if`, then each `elif` an If in the orelse of the one before, all ending where the chain
    // ends; read in a loop and closed innermost first, so a chain of any length takes no stack.
    std::uint32_t if_statement(std::uint32_t first) {
        const std::size_t base = m_if_clauses.size();   // this chain's clauses: above any enclosing chain's
        std::uint32_t end = 0;
        for (std::uint32_t f = first;; f = rows().mark()) {
            m_if_clauses.emplace_back(f, advance().start());   // `if` or `elif`
            rows().slot(parser().named_expression().row(), slot::If::test);
            end = parser().suite(slot::If::body);
            if (!at(token_kind::kw_elif)) break;
        }
        if (eat(token_kind::kw_else)) end = parser().suite(slot::If::orelse);
        std::uint32_t inner = no_value;
        for (std::size_t k = m_if_clauses.size(); k-- > base;) {
            if (inner != no_value) rows().slot(inner, slot::If::orelse);
            inner = rows().close(kind::If, m_if_clauses[k].first(), m_if_clauses[k].start(), end);
        }
        m_if_clauses.erase(m_if_clauses.begin() + static_cast<std::ptrdiff_t>(base), m_if_clauses.end());
        return inner;
    }

    std::uint32_t while_statement(std::uint32_t first) {
        const token t = advance();
        rows().slot(parser().named_expression().row(), slot::While::test);
        std::uint32_t end = parser().suite(slot::While::body);
        if (eat(token_kind::kw_else)) end = parser().suite(slot::While::orelse);
        return rows().close(kind::While, first, t.start(), end);
    }

    std::uint32_t for_statement(std::uint32_t first, std::uint32_t start, bool is_async) {
        advance();   // `for`
        const expr target = parser().target_list();
        rows().slot(target.row(), slot::For::target);
        expect(token_kind::kw_in);
        rows().slot(parser().star_expressions().row(), slot::For::iter);
        std::uint32_t end = parser().suite(slot::For::body);
        if (eat(token_kind::kw_else)) end = parser().suite(slot::For::orelse);
        return rows().close(is_async ? kind::AsyncFor : kind::For, first, start, end);
    }

    std::uint32_t with_statement(std::uint32_t first, std::uint32_t start, bool is_async) {
        advance();   // `with`
        bool done = false;
        if (at(token_kind::lpar)) {   // `with (a as b, c):` or an expression that starts with `(`
            const auto c = save();
            try {
                advance();
                for (;;) {
                    with_item();
                    if (!eat(token_kind::comma) || at(token_kind::rpar)) break;
                }
                if (at(token_kind::rpar) && kind_at(1) == token_kind::colon) {
                    advance();
                    done = true;
                }
            } catch (const syntax_error&) {
            }
            if (!done) rewind(c);
        }
        if (!done) {
            do with_item();
            while (eat(token_kind::comma));
        }
        const std::uint32_t end = parser().suite(slot::With::body);
        return rows().close(is_async ? kind::AsyncWith : kind::With, first, start, end);
    }
    void with_item() {
        const std::uint32_t w = rows().mark();
        rows().slot(parser().expression().row(), slot::withitem::context_expr);
        if (eat(token_kind::kw_as)) {
            const expr t = parser().single_target();
            rows().slot(t.row(), slot::withitem::optional_vars);
        }
        rows().slot(rows().close(kind::withitem, w, 0, 0), slot::With::items);
    }

    std::uint32_t try_statement(std::uint32_t first) {
        const token t = advance();
        std::uint32_t end = parser().suite(slot::Try::body);
        std::optional<bool> star;   // `except` or `except*`: the first handler decides
        bool handlers = false;
        while (at(token_kind::kw_except)) {
            const token e = advance();
            const bool this_star = eat(token_kind::star);
            if (!star) star = this_star;
            else if (*star != this_star) fail("cannot have both 'except' and 'except*' on the same 'try'", e.start());
            const std::uint32_t h_first = rows().mark();
            std::uint32_t name = no_value;
            if (!at(token_kind::colon)) {
                const std::uint32_t tfirst = rows().mark();
                const expr type = parser().expression();
                std::uint32_t type_row = type.row();
                if (at(token_kind::comma)) {   // PEP 758: `except A, B:` without parentheses
                    rows().slot(type.row(), slot::Tuple::elts);
                    while (eat(token_kind::comma) && !at(token_kind::colon)) rows().slot(parser().expression().row(), slot::Tuple::elts);
                    if (at(token_kind::kw_as)) fail("multiple exception types must be parenthesized when using 'as'", type.start());
                    type_row = rows().close(kind::Tuple, tfirst, type.start(), prev_end(), load);
                }
                rows().slot(type_row, slot::ExceptHandler::type);
                if (eat(token_kind::kw_as)) name = intern(expect_name());
            } else if (this_star) {
                fail_here("expected one or more exception types");
            }
            end = parser().suite(slot::ExceptHandler::body);
            rows().slot(rows().close(kind::ExceptHandler, h_first, e.start(), end, 0, name), slot::Try::handlers);
            handlers = true;
        }
        if (handlers && eat(token_kind::kw_else)) end = parser().suite(slot::Try::orelse);
        if (eat(token_kind::kw_finally)) end = parser().suite(slot::Try::finalbody);
        else if (!handlers) fail_here("expected 'except' or 'finally' block");
        return rows().close(star.value_or(false) ? kind::TryStar : kind::Try, first, t.start(), end);
    }

    std::uint32_t async_statement(std::uint32_t first) {
        const token a = advance();
        switch (tok().kind()) {
            case token_kind::kw_def: return function_def(first, a.start(), true);
            case token_kind::kw_with: return with_statement(first, a.start(), true);
            case token_kind::kw_for: return for_statement(first, a.start(), true);
            default: fail_here();
        }
    }

    std::uint32_t decorated(std::uint32_t first) {
        while (eat(token_kind::at)) {
            parser().named_expression();
            expect(token_kind::newline);
        }
        const std::uint32_t decorators_end = rows().mark();
        std::uint32_t r = 0;
        std::uint8_t field = 0;
        if (at(token_kind::kw_class)) {
            r = class_def(first, tok().start());
            field = slot::ClassDef::decorator_list;
        } else if (at(token_kind::kw_def)) {
            r = function_def(first, tok().start(), false);
            field = slot::FunctionDef::decorator_list;
        } else if (at(token_kind::kw_async) && kind_at(1) == token_kind::kw_def) {
            const token a = advance();
            r = function_def(first, a.start(), true);
            field = slot::AsyncFunctionDef::decorator_list;
        } else {
            fail_here("invalid syntax: expected 'def' or 'class' after a decorator");
        }
        for_each_root(first, decorators_end, [&](std::uint32_t d) { rows().slot(d, field); });
        return r;
    }

    std::uint32_t function_def(std::uint32_t first, std::uint32_t start, bool is_async) {
        advance();   // `def`
        const std::uint32_t name = intern(expect_name());
        if (at(token_kind::lsqb)) type_params(slot::FunctionDef::type_params);
        expect(token_kind::lpar);
        const std::uint32_t args = parameters(true, token_kind::rpar);
        rows().slot(args, slot::FunctionDef::args);
        expect(token_kind::rpar);
        if (eat(token_kind::rarrow)) rows().slot(parser().expression().row(), slot::FunctionDef::returns);
        const std::uint32_t end = parser().suite(slot::FunctionDef::body);
        return rows().close(is_async ? kind::AsyncFunctionDef : kind::FunctionDef, first, start, end, 0, name);
    }

    std::uint32_t class_def(std::uint32_t first, std::uint32_t start) {
        advance();   // `class`
        const std::uint32_t name = intern(expect_name());
        if (at(token_kind::lsqb)) type_params(slot::ClassDef::type_params);
        if (at(token_kind::lpar)) {
            const token l = advance();
            parser().arguments(slot::ClassDef::bases, slot::ClassDef::keywords, l.start(), false);
        }
        const std::uint32_t end = parser().suite(slot::ClassDef::body);
        return rows().close(kind::ClassDef, first, start, end, 0, name);
    }

    // `[T: int = str, *Ts, **P]`, each in `field` of the def, class or alias.
    void type_params(std::uint8_t field) {
        advance();   // `[`
        for (;;) {
            const std::uint32_t p = rows().mark();
            const std::uint32_t start = tok().start();
            enum kind k = kind::TypeVar;
            std::uint8_t default_slot = slot::TypeVar::default_value;
            if (eat(token_kind::star)) {
                k = kind::TypeVarTuple;
                default_slot = slot::TypeVarTuple::default_value;
            } else if (eat(token_kind::doublestar)) {
                k = kind::ParamSpec;
                default_slot = slot::ParamSpec::default_value;
            }
            const std::uint32_t name = intern(expect_name());
            if (k == kind::TypeVar && eat(token_kind::colon)) rows().slot(parser().expression().row(), slot::TypeVar::bound);
            if (eat(token_kind::equal)) rows().slot((k == kind::TypeVarTuple ? parser().star_expression() : parser().expression()).row(), default_slot);
            rows().slot(rows().close(k, p, start, prev_end(), 0, name), field);
            if (!eat(token_kind::comma) || at(token_kind::rsqb)) break;
        }
        expect(token_kind::rsqb);
    }

    // The parameters of a def (annotated, up to `)`) or a lambda (up to `:`): their arguments row.
    std::uint32_t parameters(bool annotated, token_kind end) {
        const std::uint32_t first = rows().mark();
        bool star = false, bare_star = false, kwonly = false, saw_default = false, slash = false, kwarg = false;
        while (!at(end)) {
            if (kwarg) fail_here("arguments cannot follow var-keyword argument");
            if (at(token_kind::slash)) {
                if (star || slash || rows().mark() == first) fail_here("invalid syntax: '/' must be ahead of '*' and follow a parameter");
                slash = true;
                advance();
                for_each_root(first, rows().mark(), [&](std::uint32_t r) {
                    if (rows()[r].kind() == kind::arg) rows().slot(r, slot::arguments::posonlyargs);
                });
            } else if (at(token_kind::star)) {
                if (star) fail_here("* argument may appear only once");
                star = true;
                advance();
                if (at(token_kind::comma) || at(end)) {
                    bare_star = true;
                } else {
                    rows().slot(parameter(annotated, true), slot::arguments::vararg);
                    if (at(token_kind::equal)) fail_here("var-positional argument cannot have default value");
                }
            } else if (at(token_kind::doublestar)) {
                advance();
                rows().slot(parameter(annotated, false), slot::arguments::kwarg);
                if (at(token_kind::equal)) fail_here("var-keyword argument cannot have default value");
                kwarg = true;
            } else {
                const std::uint32_t a = parameter(annotated, false);
                if (star) {
                    kwonly = true;
                    rows().slot(a, slot::arguments::kwonlyargs);
                    if (eat(token_kind::equal)) rows().slot(parser().expression().row(), slot::arguments::kw_defaults);
                    else rows().leaf(kind::absent, slot::arguments::kw_defaults, 0, no_value);
                } else {
                    rows().slot(a, slot::arguments::args);
                    if (eat(token_kind::equal)) {
                        rows().slot(parser().expression().row(), slot::arguments::defaults);
                        saw_default = true;
                    } else if (saw_default) {
                        fail("parameter without a default follows parameter with a default", rows()[a].start());
                    }
                }
            }
            if (!eat(token_kind::comma)) break;
        }
        if (bare_star && !kwonly) fail("named parameters must follow bare *", prev_end());
        return rows().close(kind::arguments, first, 0, 0);
    }

    // One parameter: its arg row. `*args: *Ts` takes a starred annotation.
    std::uint32_t parameter(bool annotated, bool vararg) {
        const std::uint32_t first = rows().mark();
        const token n = expect_name();
        if (annotated && eat(token_kind::colon)) rows().slot((vararg ? parser().star_expression() : parser().expression()).row(), slot::arg::annotation);
        return rows().close(kind::arg, first, n.start(), prev_end(), 0, intern(n));
    }

    /// How many `if`/`elif` clauses are being read, for a checkpoint; and back to that many.
    [[nodiscard]] std::size_t open_clauses() const noexcept { return m_if_clauses.size(); }
    void close_clauses_from(std::size_t count) { m_if_clauses.erase(m_if_clauses.begin() + static_cast<std::ptrdiff_t>(count), m_if_clauses.end()); }

private:
    // One `if` or `elif` of a chain while it is read: where its rows begin, and where it starts.
    class if_clause {
    public:
        constexpr if_clause(std::uint32_t first, std::uint32_t start) noexcept : m_first(first), m_start(start) {}
        [[nodiscard]] constexpr std::uint32_t first() const noexcept { return m_first; }
        [[nodiscard]] constexpr std::uint32_t start() const noexcept { return m_start; }

    private:
        std::uint32_t m_first;
        std::uint32_t m_start;
    };
    std::vector<if_clause> m_if_clauses;   // the clauses of the chains being read, innermost last: a chain allocated none
};

}  // namespace pygim::code::python::grammar
