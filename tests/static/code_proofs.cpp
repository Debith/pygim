// tests/static/code_proofs.cpp — the code component's laws, proven at compile time on every build
// (ext.code.toml lists this file among the sources). It contributes no runtime code.
//
// What each header proves under its own definitions (#253) is compiled here too: every keyword
// is found as itself (tokens.h), postorder rows pack into source order (rows.h), every node kind
// is found by its name (tree.h), the generated tables agree with the ast module's field order
// (ast_tables.gen.h) and the identifier tables answer for known characters (unicode_tables.gen.h).

#include "../../src/_pygim_fast/code/python/ast_tables.gen.h"
#include "../../src/_pygim_fast/code/python/rows.h"
#include "../../src/_pygim_fast/code/python/tokens.h"
#include "../../src/_pygim_fast/code/python/tree.h"
#include "../../src/_pygim_fast/code/python/unicode_tables.gen.h"

namespace pygim::code::python::proofs {

// Every keyword is found as itself and every soft keyword as its kind; a near miss is a name.
consteval bool every_keyword_is_found() {
    for (std::size_t i = 0; i < detail::keyword_names.size(); ++i) {
        if (keyword_of(detail::keyword_names[i]) != static_cast<token_kind>(static_cast<std::size_t>(token_kind::kw_False) + i)) return false;
    }
    return keyword_of("Match") == token_kind::name && keyword_of("iff") == token_kind::name && keyword_of("") == token_kind::name;
}
static_assert(every_keyword_is_found());

// Every kind reads as something in an error message: the operator list is as long as the operators.
consteval bool every_kind_is_spelled() {
    for (std::size_t i = 0; i < token_kind_count; ++i) {
        if (spelling(static_cast<token_kind>(i)).empty()) return false;
    }
    return true;
}
static_assert(every_kind_is_spelled());

// The operators' strengths order as Python's: | < ^ < & < shifts < + - < * / // % @.
static_assert(info(token_kind::vbar).binary_power() < info(token_kind::circumflex).binary_power() &&
              info(token_kind::circumflex).binary_power() < info(token_kind::amper).binary_power() &&
              info(token_kind::amper).binary_power() < info(token_kind::leftshift).binary_power() &&
              info(token_kind::leftshift).binary_power() < info(token_kind::plus).binary_power() &&
              info(token_kind::plus).binary_power() < info(token_kind::star).binary_power() &&
              info(token_kind::star).binary_power() == info(token_kind::doubleslash).binary_power());

// Each binary operator's index names the ast operator it is.
static_assert(info(static_cast<kind>(family::operator_ + info(token_kind::plus).binary_op())).name() == "Add" &&
              info(static_cast<kind>(family::operator_ + info(token_kind::at).binary_op())).name() == "MatMult" &&
              info(static_cast<kind>(family::operator_ + info(token_kind::doubleslashequal).aug_op() - 1)).name() == "FloorDiv" &&
              info(static_cast<kind>(family::cmpop + info(token_kind::lessequal).compare_op() - 1)).name() == "LtE" &&
              info(static_cast<kind>(family::expr_context + 1)).name() == "Store");

// A statement with two children and a grandchild: `if a: b(c)` closed as a, c, b, b(c), Expr, If
// (the Call's func `b` closes before its argument), packed parent-first in source order.
consteval bool nested_statement_packs() {
    row_writer w;
    const std::uint32_t if_first = w.mark();
    const std::uint32_t a = w.close(kind::Name, w.mark(), 3, 4);
    const std::uint32_t expr_first = w.mark();
    const std::uint32_t b = w.close(kind::Name, w.mark(), 6, 7);
    const std::uint32_t c = w.close(kind::Name, w.mark(), 8, 9);
    const std::uint32_t call = w.close(kind::Call, b, 6, 10);
    const std::uint32_t expr = w.close(kind::Expr, expr_first, 6, 10);
    const std::uint32_t stmt = w.close(kind::If, if_first, 0, 10);
    w.slot(a, slot::If::test);
    w.slot(b, slot::Call::func);
    w.slot(c, slot::Call::args);
    w.slot(call, slot::Expr::value);
    w.slot(expr, slot::If::body);
    const std::vector<node> t = pack(w.rows());
    return stmt == 5 && t[0].kind() == kind::If && t[1].kind() == kind::Name && t[1].field() == slot::If::test &&
           t[2].kind() == kind::Expr && t[2].parent() == 0 && t[3].kind() == kind::Call && t[3].parent() == 2 &&
           t[4].start() == 6 && t[4].parent() == 3 && t[5].start() == 8 && t[5].parent() == 3 && t[5].field() == slot::Call::args;
}
static_assert(nested_statement_packs());

// Two roots side by side pack one after the other (a corpus file has one, the Module; the pass
// does not depend on it).
consteval bool two_roots_pack() {
    row_writer w;
    w.close(kind::Pass, w.mark(), 0, 4);
    w.close(kind::Break, w.mark(), 5, 10);
    const std::vector<node> t = pack(w.rows());
    return t[0].kind() == kind::Pass && t[1].kind() == kind::Break && t[0].parent() == no_value && t[1].parent() == no_value;
}
static_assert(two_roots_pack());

// The identifier tables: Latin, Greek and CJK letters start names; combining marks and digits of
// other scripts only continue them; currency signs and spaces do neither.
static_assert(unicode::starts_identifier(0x00E9) && unicode::starts_identifier(0x03BC) && unicode::starts_identifier(0x87D2));
static_assert(!unicode::starts_identifier(0x0301) && unicode::continues_identifier(0x0301) && unicode::continues_identifier(0x0660));
static_assert(!unicode::continues_identifier(0x20AC) && !unicode::continues_identifier(0x00A0) && !unicode::continues_identifier(0x2028));

// The parser is a facade (D60): module() is its face; its parts' rules and its cursor are not.
template <class P> concept shows_module = requires(P& p) { p.module(); };
template <class P> concept shows_a_rule = requires(P& p) { p.expression(); } || requires(P& p) { p.statement(0); } ||
                                          requires(P& p) { p.strings(); } || requires(P& p) { p.cursor(); };
static_assert(shows_module<parser> && !shows_a_rule<parser>);

}  // namespace pygim::code::python::proofs
