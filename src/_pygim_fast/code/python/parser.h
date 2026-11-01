#pragma once
// code/python/parser.h — Python 3.14 source to the rows of its syntax tree, by hand-written
// recursive descent, on Ruff's layering (#280): statements, expressions by operator strength
// (Pratt), patterns; tokens pulled from the lexer as needed, one past the current at most.
//
//     parse_output out;
//     hashed_interner names;
//     basic_parser<> p(src.text(), out, names);
//     p.module();                  // throws syntax_error at the first error (Q35)
//     syntax_tree t(std::move(src), std::move(out));   // tree.h: rows packed to preorder
//
// basic_parser is a facade (D60): module() is all it shows, and it is made of components that
// each own one thing —
//
//     token_cursor        cursor.h                   the current token and the one after it
//     parse_output        output.h                   the rows and literals written
//     statement_rules     grammar/statements.h       a statement, a line of them, a block; imports
//     compound_rules      grammar/compound.h         if, while, for, with, try, def, class, parameters
//     pattern_rules       grammar/patterns.h         match and its patterns
//     expression_rules    grammar/expressions.h      expressions by strength, calls, comprehensions, targets
//     atom_rules          grammar/atoms.h            names, numbers, constants, the bracketed displays
//     string_rules        grammar/strings.h          strings side by side, f- and t-strings
//     string_pieces       grammar/string_pieces.h    a string's text before it is a literal
//     make_target         grammar/targets.h          an expression made a target in place
//     future_imports      grammar/future_imports.h   the `from __future__` imports a file opens with
//
// The rules are parts of one parser (part.h): each calls another part's rule through parser(),
// statically, so the split costs no call that the one class did not make.
//
// The node kinds and fields are CPython's (ast_tables.gen.h, D54), and so are the positions —
// where they differ from Ruff's: a decorated def starts at `def`, a generator that is a call's
// only argument spans the call's parentheses, implicitly concatenated strings are one node.
//
// How it stays small and fast:
//   * Parse a superset, then check (#280). An assignment's targets are parsed as expressions and
//     then turned into targets in place (their context set, their shape checked) in one walk; a
//     keyword argument is told from an expression by the `=` after its name, one token ahead.
//   * Rows are written once, when a node closes (rows.h); nothing is built to be thrown away. The
//     two places Python's grammar is ambiguous to a reader of two tokens — `with (` and a
//     statement starting with `match` — take a checkpoint (a copy of the lexer's state, the
//     token ring and the counts of rows) and rewind on failure: about twice per 600 files.
//   * A token is 12 bytes and holds no text; names are interned into the caller's table.
//
// Not yet (decided 2026-10-25, alternatives in docs/design/code_parser.md): error recovery; error
// messages word for word as CPython's; type comments; versions before 3.14 (version gates).
//
// CORE layer: pybind-free.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "../../mapping/intern.h"
#include "cursor.h"
#include "grammar/atoms.h"
#include "grammar/compound.h"
#include "grammar/expressions.h"
#include "grammar/future_imports.h"
#include "grammar/patterns.h"
#include "grammar/statements.h"
#include "grammar/strings.h"
#include "output.h"
#include "source.h"

namespace pygim::code::python {

/// The error policy: the first error ends the parse (Q35). A recovering policy is the next one.
class stop_at_first {
public:
    [[noreturn]] static void fail(std::string message, std::uint32_t offset) { throw syntax_error(std::move(message), offset); }
};

template <class Errors = stop_at_first, mapping::interner Names = mapping::hashed_interner>
class basic_parser
    : private grammar::statement_rules<basic_parser<Errors, Names>>,
      private grammar::compound_rules<basic_parser<Errors, Names>>,
      private grammar::pattern_rules<basic_parser<Errors, Names>>,
      private grammar::expression_rules<basic_parser<Errors, Names>>,
      private grammar::atom_rules<basic_parser<Errors, Names>>,
      private grammar::string_rules<basic_parser<Errors, Names>> {
    using statement_part = grammar::statement_rules<basic_parser>;
    using compound_part = grammar::compound_rules<basic_parser>;
    using pattern_part = grammar::pattern_rules<basic_parser>;
    using expression_part = grammar::expression_rules<basic_parser>;
    using atom_part = grammar::atom_rules<basic_parser>;
    using string_part = grammar::string_rules<basic_parser>;

public:
    using errors = Errors;
    static constexpr int max_depth = grammar::max_depth;   // nested expressions and statements, beyond the lexer's limits

    basic_parser(std::string_view text, parse_output& out, Names& names) : m_cursor(text), m_out(out), m_names(names) {}

    /// The whole file: its statements, then the Module row, last.
    void module() {
        row_writer& rows = m_out.rows();
        const std::uint32_t first = rows.mark();
        grammar::future_imports opens;   // the docstring and `from __future__` imports a file may open with
        while (m_cursor.tok().kind() != token_kind::end_of_file) {
            const std::uint32_t line = rows.mark();
            statement_part::statement(slot::Module::body);
            if (opens.open()) opens.check<Errors>(rows, m_names, line, rows.mark());   // this line's statements, while it may still open
        }
        rows.close(kind::Module, first, 0, 0);
    }

private:
    template <class, class> friend class grammar::part_of;
    friend statement_part;
    friend compound_part;
    friend pattern_part;
    friend expression_part;
    friend atom_part;
    friend string_part;

    // Where the parser stood: a memento only the parser reads (D58) — the cursor's, what was
    // written, and the strings and `if` chains being read.
    class checkpoint {
        friend class basic_parser;
        checkpoint(typename token_cursor<Errors>::state cursor, parse_output::mark written, std::size_t pieces, std::size_t clauses) noexcept
            : m_cursor(std::move(cursor)), m_written(written), m_pieces(static_cast<std::uint32_t>(pieces)), m_clauses(static_cast<std::uint32_t>(clauses)) {}
        typename token_cursor<Errors>::state m_cursor;
        parse_output::mark m_written;
        std::uint32_t m_pieces;
        std::uint32_t m_clauses;
    };

    // ── what the parts read through part_of ──
    [[nodiscard]] token_cursor<Errors>& cursor() noexcept { return m_cursor; }
    [[nodiscard]] parse_output& output() noexcept { return m_out; }
    [[nodiscard]] Names& names() noexcept { return m_names; }
    [[nodiscard]] grammar::nesting<token_cursor<Errors>> nest() { return {m_depth, m_cursor}; }
    [[nodiscard]] checkpoint save() const {
        return {m_cursor.save(), m_out.written(), string_part::pending_pieces(), compound_part::open_clauses()};
    }
    void rewind(const checkpoint& c) {
        m_cursor.restore(c.m_cursor);
        m_out.rewind(c.m_written);
        string_part::drop_pieces_from(c.m_pieces);
        compound_part::close_clauses_from(c.m_clauses);   // a speculation that threw inside an `if` chain leaves none of its clauses
    }

    token_cursor<Errors> m_cursor;
    parse_output& m_out;
    Names& m_names;
    int m_depth = 0;
};

/// The parser with its defaults: stops at the first error, names in a hashed interner.
using parser = basic_parser<>;

}  // namespace pygim::code::python
