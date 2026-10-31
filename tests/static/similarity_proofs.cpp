// Compile-time proofs for similarity/core.h, similarity/cpp_source.h and
// similarity/names.h — the normalisation, score, assignment and corpus laws,
// the C++ frontend's units, name resolution, and that two languages can never
// be compared.
//
// This TU is part of the pygim.similarity extension's SOURCES
// (ext.similarity.toml): it is compiled by every build, so a violated law
// cannot produce a binary. It contributes no runtime code.
//
// Every proof is a consteval function returning a bool computed on local
// objects (nothing string-shaped escapes into the assertion expression),
// asserted directly under it.

#include "../../src/_pygim_fast/similarity/core.h"
#include "../../src/_pygim_fast/similarity/cpp_source.h"
#include "../../src/_pygim_fast/similarity/names.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace pygim::similarity;

// Two synthetic languages: the laws are about the tag, not a frontend.
class lang_a {
public:
    static constexpr std::array<std::string_view, 2> exts{".a", ".aa"};
    static constexpr language_info info{"a", "synthetic", exts};
};
class lang_b {
public:
    static constexpr std::array<std::string_view, 1> exts{".b"};
    static constexpr language_info info{"b", "synthetic", exts};
};
class lang_a_again {   // claims lang_a's extension
public:
    static constexpr std::array<std::string_view, 1> exts{".aa"};
    static constexpr language_info info{"c", "synthetic", exts};
};

// `def f(<param>): return <param> + <rhs>` as a tree of the Python frontend's shape (its own kinds,
// not the frontend's: "FunctionDef.body()" is no field). `global` is the name `rhs` reads, a global
// never bound; empty, `rhs` is the literal 1.
constexpr tree adder(std::string_view param, std::string_view global) {
    tree_builder b;
    b.open("FunctionDef");
    b.open("arguments");
    b.open("arguments.args");
    b.leaf("arg.arg", param, role::bind);
    b.close();
    b.close();
    b.open("FunctionDef.body()");
    b.open("Return");
    b.open("BinOp");
    b.leaf("Name", param, role::use);
    b.leaf("Add");
    if (!global.empty()) b.leaf("Name", global, role::use);
    else b.leaf("Constant", "int", role::constant);
    b.close();
    b.close();
    b.close();
    b.close();
    return std::move(b).finish();
}

// ── normalisation and scores (the worked example of core.h) ────────────────
consteval bool renaming_laws() {
    const auto a = fingerprint_of(adder("a", ""));
    const auto b = fingerprint_of(adder("b", ""));
    const auto n = fingerprint_of(adder("a", "n"));
    const auto m = fingerprint_of(adder("a", "m"));
    bool ok = a.root() == b.root() && dice(a, b).exact() && dice(a, b).value() == 1.0;   // a local's name never counts
    ok = ok && n.root() != m.root() && !dice(n, m).exact();                               // a global's does: `+ n` is not `+ m`
    ok = ok && !dice(a, n).exact() && dice(a, n).value() > 0.0 && dice(a, n).value() < 1.0;   // nor is a literal a global
    return ok;
}
static_assert(renaming_laws());
// The adder has 10 nodes; its weight counts every subtree of two or more
// nodes once — FunctionDef, arguments, arguments.args, FunctionDef.body(),
// Return, BinOp — and no leaf (arg.arg, Name, Add, Constant): 6.
consteval bool weight_law() {
    const auto fp = fingerprint_of(adder("a", ""));
    return fp.weight() == 6 && fp.nodes() == 10 && fp.subtrees().size() == 6;
}
static_assert(weight_law());

// A lone leaf counts as the root: two one-node units still have a fingerprint,
// and different leaves are not identical.
consteval bool leaf_root_law() {
    tree_builder a, b;
    a.leaf("Name", "x", role::attr);
    b.leaf("Name", "y", role::attr);
    const auto fa = fingerprint_of(std::move(a).finish()), fb = fingerprint_of(std::move(b).finish());
    return fa.weight() == 1 && !dice(fa, fb).exact() && dice(fa, fa).exact();
}
static_assert(leaf_root_law());

consteval bool score_laws() {
    const auto a = fingerprint_of(adder("a", ""));
    const auto c = fingerprint_of(adder("x", "n"));
    bool ok = dice(a, c).shared() == dice(c, a).shared() && dice(a, c).total() == dice(c, a).total();   // symmetric
    ok = ok && shared_weight(a, a) == a.weight() && dice(a, a).exact();                           // reflexive
    ok = ok && dice(a, c).value() <= ceiling(a.weight(), c.weight());                               // the ceiling bounds
    ok = ok && score{}.value() == 1.0 && score{0, 10}.value() == 0.0 && score{3, 10}.value() == 0.6;
    return ok;
}
static_assert(score_laws());

// A bound name is renamed wherever it appears, before its binding included;
// the order of first appearance is the name.
consteval bool first_appearance_law() {
    auto two = [](std::string_view x, std::string_view y) {
        tree_builder b;
        b.open("f");
        b.leaf("Name", x, role::use);    // read before bound
        b.leaf("Name", y, role::bind);
        b.leaf("Name", x, role::bind);
        b.close();
        return fingerprint_of(std::move(b).finish());
    };
    const auto p = two("i", "j");
    const auto q = two("u", "v");
    const auto r = two("j", "i");
    return p.root() == q.root() && p.root() == r.root();
}
static_assert(first_appearance_law());

// The role is part of a node's hash. Roles that are never renamed — an attribute `.x`, a literal
// type "x", a global read `x` — tell it alone, so dropping the role from the hash fails this law (a
// bound `x` against `.x` would not: the bound one is renamed anyway).
consteval bool role_law() {
    auto root = [](role r) {
        tree_builder b;
        b.leaf("id", "x", r);
        return fingerprint_of(std::move(b).finish()).root();
    };
    return root(role::attr) != root(role::constant) && root(role::attr) != root(role::use) && root(role::use) != root(role::constant) &&
           root(role::bind) != root(role::attr);
}
static_assert(role_law());

// ── the assignment ─────────────────────────────────────────────────────────
consteval bool assignment_laws() {
    const std::vector<std::uint64_t> w{5, 4, 4, 1};               // 2 x 2
    bool ok = best_assignment(w, 2, 2) == 8;                      // 4 + 4, not the greedy 5 + 1
    const std::vector<std::uint64_t> wide{1, 9, 2, 8, 7, 3};      // 2 x 3
    const std::vector<std::uint64_t> tall{1, 8, 9, 7, 2, 3};      // its transpose, 3 x 2
    ok = ok && best_assignment(wide, 2, 3) == 17 && best_assignment(tall, 3, 2) == 17;
    ok = ok && best_assignment({}, 0, 3) == 0;
    return ok;
}
static_assert(assignment_laws());

// The best of every way to pair each element of the smaller side with a distinct one of the larger:
// what the assignment is defined as, by enumeration.
constexpr std::uint64_t brute_assignment(const std::vector<std::uint64_t>& w, std::size_t rows, std::size_t cols, std::size_t row = 0,
                                         std::uint32_t taken = 0) {
    if (rows > cols) {   // pair the columns instead
        std::vector<std::uint64_t> t(w.size());
        for (std::size_t r = 0; r < rows; ++r)
            for (std::size_t c = 0; c < cols; ++c) t[c * rows + r] = w[r * cols + c];
        return brute_assignment(t, cols, rows);
    }
    if (row == rows) return 0;
    std::uint64_t best = 0;
    for (std::size_t c = 0; c < cols; ++c) {
        if (taken >> c & 1) continue;
        best = std::max(best, w[row * cols + c] + brute_assignment(w, rows, cols, row + 1, taken | (std::uint32_t{1} << c)));
    }
    return best;
}

// The solver's assignment is the enumeration's on 400 seeded matrices up to 4 x 5 — small weights
// full of ties, wide ones, weights just under 2^28 (the last its 32-bit potentials hold) and hubs
// past 32 bits (next() gives 31-bit values), zero rows, one hub column every row wants best — in both orientations, its buffers kept
// from matrix to matrix as a class row keeps them.
consteval bool assignment_enumeration_law() {
    std::uint64_t seed = 1;
    auto next = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return seed >> 33;
    };
    assignment_solver solver;
    bool ok = true;
    for (int k = 0; k < 400; ++k) {
        const std::size_t rows = 1 + next() % 4, cols = 1 + next() % 5;
        const std::uint64_t scale = k % 3 == 0 ? 3 : k % 3 == 1 ? 1000 : std::uint64_t{1} << (k % 2 ? 28 : 40);
        std::vector<std::uint64_t> w(rows * cols), t(rows * cols);
        for (std::uint64_t& x : w) x = next() % scale;
        if (k % 5 == 0) {
            for (std::size_t r = 0; r < rows; ++r) w[r * cols] = scale + next() % 4;   // the hub
        }
        if (k % 7 == 0) {
            for (std::size_t c = 0; c < cols; ++c) w[c] = 0;   // a row sharing nothing
        }
        for (std::size_t r = 0; r < rows; ++r)
            for (std::size_t c = 0; c < cols; ++c) t[c * rows + r] = w[r * cols + c];
        const std::uint64_t want = brute_assignment(w, rows, cols);
        ok = ok && solver.best(w, rows, cols) == want && solver.best(t, cols, rows) == want && assignment_solver::bound(w, rows, cols) >= want;
    }
    return ok;
}
static_assert(assignment_enumeration_law());

// ── the corpus ─────────────────────────────────────────────────────────────
consteval bool corpus_laws() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src.add(parsed_unit(unit_kind::function, "f", 1, adder("a", ""), {}));
    src.add(parsed_unit(unit_kind::function, "g", 2, adder("b", ""), {}));
    src.add(parsed_unit(unit_kind::function, "h", 3, adder("a", "n"), {}));
    c.add(std::move(src), "m.a()");
    const auto all = c.pairs(unit_kind::function);
    bool ok = all.size() == 3;                                                   // every combination: 3 choose 2
    ok = ok && all[0].a() == 0 && all[0].b() == 1 && all[0].score().exact();               // best first
    ok = ok && all[1].score().value() == all[2].score().value() && all[1].a() == 0 && all[2].a() == 1;   // ties in index order
    ok = ok && c.pairs(unit_kind::function, 1.0).size() == 1;                    // the threshold keeps only exact
    ok = ok && c.pairs(unit_kind::class_).empty();
    ok = ok && c.pairs(unit_kind::function, 0.0, 10).size() == 3 && c.pairs(unit_kind::function, 0.0, 11).empty();
    return ok;
}
static_assert(corpus_laws());

// Any partition of the sweep's rows, each part in its own collector, gives
// pairs(): what lets the adapter spread the rows over threads. With a limit
// too — every part's floor rises on its own — and limit k is pairs()'s
// first k, for every k.
consteval bool sweep_partition_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    const std::array<std::string_view, 6> params{"a", "b", "c", "d", "e", "f"};
    for (std::size_t i = 0; i < params.size(); ++i) {
        src.add(parsed_unit(unit_kind::function, "f", 1, adder(params[i], i % 3 == 1 ? "n" : ""), {}));
    }
    c.add(std::move(src), "m.a()");
    const auto all = c.pairs(unit_kind::function);
    const auto order = c.sweep_order(unit_kind::function, 0);
    auto same = [](const matches& x, const matches& y, std::size_t n) {
        if (x.size() != n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (x[i].a() != y[i].a() || x[i].b() != y[i].b() || x[i].score().shared() != y[i].score().shared()) return false;
        }
        return true;
    };
    bool ok = all.size() == 15;
    for (std::size_t limit = 0; ok && limit <= all.size() + 1; ++limit) {
        collector odd{0.0, limit}, even{0.0, limit};
        for (std::size_t row = 0; row < order.size(); ++row) sweep_rows(c).sweep_row(order, row, row % 2 ? odd : even);
        odd.merge(std::move(even));
        const auto merged = corpus<lang_a>::finish(std::move(odd));
        const std::size_t n = limit == 0 || limit > all.size() ? all.size() : limit;
        ok = same(merged, all, n) && same(c.pairs(unit_kind::function, 0.0, 0, limit), all, n);
    }
    return ok;
}
static_assert(sweep_partition_law());

consteval bool class_laws() {
    // Two classes with the same two methods in opposite order: exact.
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    auto header = [] {
        tree_builder b;
        b.open("ClassDef");
        b.leaf("Name", "Base", role::use);
        b.close();
        return std::move(b).finish();
    };
    src.add(parsed_unit(unit_kind::class_, "P", 1, header(), {1, 2}));
    src.add(parsed_unit(unit_kind::function, "P.m", 2, adder("a", ""), {}));
    src.add(parsed_unit(unit_kind::function, "P.n", 3, adder("a", "n"), {}));
    src.add(parsed_unit(unit_kind::class_, "Q", 4, header(), {4, 5}));
    src.add(parsed_unit(unit_kind::function, "Q.n", 5, adder("z", "n"), {}));
    src.add(parsed_unit(unit_kind::function, "Q.m", 6, adder("z", ""), {}));
    src.add(parsed_unit(unit_kind::class_, "R", 7, header(), {7}));      // only one method
    src.add(parsed_unit(unit_kind::function, "R.m", 8, adder("q", ""), {}));
    c.add(std::move(src), "m.a()");
    const auto pq = c.compare(0, 3);
    const auto pr = c.compare(0, 6);
    bool ok = pq.exact();                                       // methods paired by best assignment, not order
    ok = ok && !pr.exact() && pr.value() > 0.0;                 // an unmatched method counts against
    ok = ok && c.units()[0].weight() == c.units()[0].fp().weight() + 6 + 6;
    ok = ok && c.pairs(unit_kind::class_).size() == 3 && c.pairs(unit_kind::function).size() == 10;
    return ok;
}
static_assert(class_laws());

// ── languages never meet ───────────────────────────────────────────────────
template <class C, class S>
concept accepts = requires(C& c, S s) { c.add(std::move(s), ""); };

static_assert(accepts<corpus<lang_a>, parsed_source<lang_a>>);
static_assert(!accepts<corpus<lang_a>, parsed_source<lang_b>>, "a corpus accepts only its own language's units");
static_assert(!accepts<corpus<lang_b>, parsed_source<lang_a>>);
static_assert(!std::is_convertible_v<corpus<lang_a>, corpus<lang_b>>);
static_assert(languages_disjoint<lang_a, lang_b>());
static_assert(!languages_disjoint<lang_a, lang_a_again>(), "one extension, two languages: refused");
static_assert(!languages_disjoint<lang_a, lang_a>(), "one name twice: refused");

// ── the C++ frontend (the worked example of cpp_source.h) ──────────────────
constexpr std::string_view geo = R"(#include <cstdlib>
namespace geo {
struct Point {
    int x = 0;
    int norm1() const { return abs(x) + abs(y); }
};
int twice(int v) { return v * 2; }
int doubled(int w) { return w * 2; }
long widened(long v) { return v * 2; }
}
)";

consteval bool cpp_units_law() {
    const auto u = cpp::parse_cpp(geo, "geo.h");
    bool ok = u.size() == 5;
    ok = ok && u[0].kind() == unit_kind::class_ && u[0].qualname() == "geo::Point" && u[0].line() == 3;
    ok = ok && u[0].methods().size() == 1 && u[0].methods()[0] == 1;
    ok = ok && u[1].qualname() == "geo::Point::norm1" && u[1].line() == 5;
    ok = ok && u[2].qualname() == "geo::twice" && u[3].qualname() == "geo::doubled" && u[4].qualname() == "geo::widened";
    return ok;
}
static_assert(cpp_units_law());

consteval bool cpp_scores_law() {
    auto u = cpp::parse_cpp(geo, "geo.h");
    const auto twice = fingerprint_of(u[2].body());
    const auto doubled = fingerprint_of(u[3].body());
    const auto widened = fingerprint_of(u[4].body());
    return dice(twice, doubled).exact()                    // a parameter's name never counts
           && !dice(twice, widened).exact()                // its type does
           && dice(twice, widened).value() >= 0.5;    // 2 of 4 counted subtrees differ: params and the root
}
static_assert(cpp_scores_law());

consteval bool cpp_structure_laws() {
    const auto u = cpp::parse_cpp(R"(
template <class T, int N = (3 > 2)>
class Box : public Base<T> {
public:
    Box(int a) : n(a), m{a} { }
    ~Box() { }
    bool operator==(const Box&) const = default;
    int operator()(int q) const noexcept { return q; }
    auto size() const -> int { return n; }
private:
    int n, m;
};
int Box::far(double z) { return 0; }
int value = compute(1);
extern "C" { void c_api(void) { } }
namespace { int hidden() { return 1; } }
class Fwd;
struct S s;
)", "box.h");
    bool ok = u.size() == 8;
    ok = ok && u[0].qualname() == "Box" && u[0].methods().size() == 4;
    ok = ok && u[1].qualname() == "Box::Box" && u[2].qualname() == "Box::~Box" && u[3].qualname() == "Box::operator()";
    ok = ok && u[4].qualname() == "Box::size" && u[5].qualname() == "Box::far" && u[6].qualname() == "c_api";
    ok = ok && u[7].qualname() == "{anonymous}::hidden";
    return ok;
}
static_assert(cpp_structure_laws());

consteval bool cpp_tokenizer_laws() {
    const auto t = cpp::tokenize(R"T(a+=1'000; /* c
*/ R"x(raw ")" )x" u8"s" 'c' 0x1p3 // tail
#define X \
  continued
b)T");
    bool ok = t.size() == 9;
    ok = ok && t[0].text() == "a" && t[1].text() == "+=" && t[2].text() == "1'000" && t[3].text() == ";";
    ok = ok && t[4].kind() == cpp::tok::string && t[4].line() == 2;
    ok = ok && t[5].kind() == cpp::tok::string && t[6].kind() == cpp::tok::character;
    ok = ok && t[7].kind() == cpp::tok::number && t[8].text() == "b" && t[8].line() == 5;
    return ok;
}
static_assert(cpp_tokenizer_laws());

// ── names (the worked example of names.h) ──────────────────────────────────
consteval bool binding_laws() {
    binding_table::builder b;
    b.bind("osp", "os.path");
    b.bind("j", "os.path.join");
    b.bind("json", "ujson");
    b.bind("osp", "os.path");                // the same target again: still one binding
    const binding_table so_far = b.built();  // and binding goes on
    b.bind("json", "json");                  // a second target: ambiguous
    const binding_table t = std::move(b).build();
    bool ok = t.resolve("osp") == std::optional<std::string_view>("os.path") && t.size() == 3;
    ok = ok && !t.resolve("json") && t.binds("json") && so_far.resolve("json") == std::optional<std::string_view>("ujson");
    ok = ok && !t.resolve("x") && !t.binds("x") && t.resolve("j") == std::optional<std::string_view>("os.path.join");
    return ok;
}
static_assert(binding_laws());

consteval bool relative_laws() {
    bool ok = relative_module("pkg.sub", 1, "m") == std::optional<std::string>("pkg.sub.m");
    ok = ok && relative_module("pkg.sub", 2, "") == std::optional<std::string>("pkg");
    ok = ok && relative_module("pkg.sub", 2, "x.y") == std::optional<std::string>("pkg.x.y");
    ok = ok && !relative_module("pkg", 3, "m") && !relative_module("", 1, "m");
    ok = ok && relative_module("", 0, "os.path") == std::optional<std::string>("os.path");
    ok = ok && without_builtins("builtins.len") == "len" && without_builtins("os.sep") == "os.sep";
    const auto parts = split_name("os.path.join", ".");
    ok = ok && parts.size() == 3 && parts[0] == "os" && parts[2] == "join";
    return ok;
}
static_assert(relative_laws());

// The same calls, spelled through aliases and using-declarations, are one tree.
consteval bool cpp_names_law() {
    const auto u = cpp::parse_cpp(R"(
namespace fs = std::filesystem;
using std::move;
int a(int x) { return std::move(fs::path(x)); }
int b(int y) { return ::std::move(std::filesystem::path(y)); }
int c(int z) { using std::move; return move(std::filesystem::path(z)); }
int d(int move) { return move(fs::path(move)); }
)", "names.cpp");
    const auto fa = fingerprint_of(u[0].body()), fb = fingerprint_of(u[1].body()), fc = fingerprint_of(u[2].body()),
               fd = fingerprint_of(u[3].body());
    return u.size() == 4 && dice(fa, fb).exact() && dice(fa, fc).exact()
           && !dice(fa, fd).exact();                     // a parameter named move is not std::move
}
static_assert(cpp_names_law());

// One branch of every conditional: braces opened in each branch balance, and
// `#if 0` is dead code.
consteval bool cpp_conditionals_law() {
    const auto u = cpp::parse_cpp(R"(
#ifdef __cplusplus
extern "C" {
#endif
int f(long p) {
#if SIZEOF_VOID_P == SIZEOF_LONG
    if (audit("kO", p) < 0) {
#elif SIZEOF_VOID_P == SIZEOF_LONG_LONG
    if (audit("KO", p) < 0) {
#else
#   warning "don't know"
    if (audit("nO", p) < 0) {
#endif
        return -1;
    }
    return 0;
}
static_assert(cpp_conditionals_law());
#if 0
int dead(int x) { return x; }
#else
int live(int x) { return x + 1; }
#endif
#ifdef __cplusplus
}
#endif
)", "callproc.c");
    return u.size() == 2 && u[0].qualname() == "f" && u[1].qualname() == "live";
}

// A block comment inside a multi-line #define belongs to the directive, even
// across a line with no backslash.
consteval bool cpp_directive_comment_law() {
    const auto t = cpp::tokenize("#define M(x) \\\n    if (x) { /* spans\n   lines */ \\\n    }\nint y;\n");
    const auto u = cpp::tokenize("#include \"gen.h\"   // includes engines/*.h\n"
                                 "#include \"engines/*.h\"\nint z;\n");
    return t.size() == 3 && t[0].text() == "int" && t[2].text() == ";" && t[0].line() == 5
           && u.size() == 3 && u[1].text() == "z" && u[1].line() == 3;   // `/*` in a // comment or a string opens nothing
}
static_assert(cpp_directive_comment_law());

// A string literal spliced over lines (a docstring macro) counts those lines:
// the string starts on its own line and what follows is where it is.
consteval bool cpp_spliced_string_law() {
    const auto t = cpp::tokenize("s = \"a\\\nb\\\nc\";\nint y;\n");
    // in a continued directive too, where '"' is a character, not a string's start
    const auto u = cpp::tokenize("#define Q(o) o = '\"'; \\\n  o = '\"'; \\\n  o = 0\nint z;\n");
    return t.size() == 7 && t[2].kind() == cpp::tok::string && t[2].line() == 1 && t[3].line() == 3
           && t[4].text() == "int" && t[4].line() == 4 && u.size() == 3 && u[0].line() == 4;
}
static_assert(cpp_spliced_string_law());

// Assigning a member is not declaring a local: different members differ;
// a local is renamed wherever it is assigned, through its declaration.
consteval bool cpp_assignment_law() {
    const auto u = cpp::parse_cpp(R"(
struct Q {
    Q& from_table(int table) { m_table = table; return *this; }
    Q& where(int clause) { m_where = clause; return *this; }
};
int f(int a) { int r = 0; r = a + 1; return r; }
int g(int b) { int s = 0; s = b + 1; return s; }
)", "q.h");
    const auto ft = fingerprint_of(u[1].body()), wh = fingerprint_of(u[2].body());
    const auto f = fingerprint_of(u[3].body()), g = fingerprint_of(u[4].body());
    return u.size() == 5 && !dice(ft, wh).exact() && dice(f, g).exact();
}
static_assert(cpp_assignment_law());

// A name binds after a type, where declarations are read; after `>`, `&` or `not` in a condition
// or a call's arguments it is an operand, a global that counts. A template's `>>` closes a type,
// and `*`, `&`, `&&` after one declare.
consteval bool cpp_binding_law() {
    const auto u = cpp::parse_cpp(R"(
int a(int n) { if (n > limit) return 1; return 0; }
int b(int n) { if (n > bound) return 1; return 0; }
int c(int n) { return h(n & mask); }
int d(int n) { return h(n & flags); }
bool e(bool x) { if (not done) return x; return !x; }
bool f(bool x) { if (not ready) return x; return !x; }
int g(int n) { std::map<K, std::vector<V>> m; return n; }
int i(int n) { std::map<K, std::vector<V>> q; return n; }
int j(int n, int* p, int& r, int&& s) { return n + *p + r + s; }
int k(int m, int* q, int& t, int&& w) { return m + *q + t + w; }
)", "bind.cpp");
    const auto fp = [&](std::size_t i) { return fingerprint_of(u[i].body()); };
    return u.size() == 10 && !dice(fp(0), fp(1)).exact() && !dice(fp(2), fp(3)).exact() && !dice(fp(4), fp(5)).exact() &&
           dice(fp(6), fp(7)).exact() && dice(fp(8), fp(9)).exact();
}
static_assert(cpp_binding_law());

// A class head's `alignas(...)` and a macro before its name are no name; `inline namespace` is a
// scope; `module` and `final` are names; CRLF lines splice in a string and continue a `//`
// comment — whose next line, swallowed, would otherwise unbalance the text.
consteval bool cpp_heads_law() {
    const auto u = cpp::parse_cpp("struct alignas(16) S { int f() { return 1; } };\r\n"
                                  "class LLVM_ABI Foo final { int g(int module) { return module; } };\r\n"
                                  "inline namespace v1 { int h(int final) { return final; } }\r\n"
                                  "// a note \\\r\nint not_code = (;\r\n"
                                  "int k(int x) { const char* s = \"a\\\r\nb\"; return x; }\r\n"
                                  "int m(int y) { return y; }\r\n",
                                  "heads.cpp");
    bool ok = u.size() == 7 && u[0].qualname() == "S" && u[1].qualname() == "S::f" && u[2].qualname() == "Foo" && u[3].qualname() == "Foo::g";
    ok = ok && u[4].qualname() == "v1::h" && u[5].qualname() == "k" && u[5].line() == 6 && u[6].line() == 8;
    const auto g = fingerprint_of(u[3].body()), h = fingerprint_of(u[4].body()), m = fingerprint_of(u[6].body());
    return ok && dice(g, m).exact() && dice(h, m).exact();   // `module` and `final` renamed as parameters
}
static_assert(cpp_heads_law());

// The prefix filter (token_index, sweep_rows::filtered_row) finds exactly the sweep's pairs at
// every threshold it serves: a family of near-copies, functions with repeated subtrees, and
// strangers, compared every way.
constexpr std::string_view family = R"(
int sum2(int a, int b) { int s = a + b; if (s > 10) { return s * 2; } return s; }
int sum3(int a, int b) { int s = a + b; if (s > 10) { return s * 3; } return s; }
int sum4(int a, int b) { int s = a - b; if (s > 10) { return s * 2; } return s; }
int sum5(int a, int b) { int s = a + b; if (s > 20) { return s * 2; } else { return -s; } }
int rep1(int a) { a = a + 1; a = a + 1; a = a + 1; return a; }
int rep2(int a) { a = a + 1; a = a + 1; return a; }
int rep3(int a) { a = a + 1; a = a + 2; a = a + 1; a = a + 1; return a; }
long other(long x, long y) { while (x < y) { x = x * 3 + y; } return x - y; }
bool test(char c) { return c == 'a' || c == 'b' || c == 'c'; }
int sum6(int a, int b) { int s = a + b; if (s > 10) { return s * 2; } return s; }
)";
consteval bool prefix_filter_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(family, "family.cpp"));
    c.add(std::move(src), "family.cpp");
    const auto order = c.sweep_order(unit_kind::function, 0);
    bool ok = order.size() == 10;
    for (double t : {0.5, 0.6, 0.7, 0.75, 0.8, 0.9, 1.0}) {
        const token_index index(c.units(), order, t);
        for (std::size_t limit : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{5}, std::size_t{100}}) {   // a limit's rising floor too
            std::vector<std::uint32_t> seen(order.size(), 0);
            collector filtered(t, limit);
            for (std::size_t r = 0; r < order.size(); ++r) sweep_rows(c).filtered_row(index, order, r, seen, filtered);
            const auto got = corpus<lang_a>::finish(std::move(filtered));
            const auto want = c.pairs(unit_kind::function, t, 0, limit);
            ok = ok && got.size() == want.size();
            for (std::size_t i = 0; ok && i < got.size(); ++i) {
                ok = got[i].a() == want[i].a() && got[i].b() == want[i].b() && got[i].score().shared() == want[i].score().shared() && got[i].score().total() == want[i].score().total();
            }
        }
    }
    // the family has pairs to find at 0.5 that 0.8 leaves out, and identical triplets at 1.0: sum2, sum3
    // and sum6 (a constant counts by its type, so `* 2` and `* 3` are one subtree)
    return ok && c.pairs(unit_kind::function, 0.5, 0, 0).size() == 13 && c.pairs(unit_kind::function, 1.0, 0, 0).size() == 3;
}
static_assert(prefix_filter_law());

// A filtered row in sweep order (sweep_rows::filtered_row_in_order, a stream's at a threshold)
// keeps sweep_row()'s pairs at that threshold in the same order, partner by partner, whatever
// order the index offers them in.
consteval bool filtered_order_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(family, "family.cpp"));
    c.add(std::move(src), "family.cpp");
    const auto order = c.sweep_order(unit_kind::function, 0);
    bool ok = true;
    for (double t : {0.5, 0.7, 0.8, 1.0}) {
        const token_index index(c.units(), order, t);
        std::vector<std::uint32_t> seen(order.size(), 0), partners{7, 7, 7};   // any contents: cleared
        collector swept(t, 0), filtered(t, 0);
        for (std::size_t r = 0; r < order.size(); ++r) {
            sweep_rows(c).sweep_row(order, r, swept);
            sweep_rows(c).filtered_row_in_order(index, order, r, seen, partners, filtered);
        }
        const matches& want = swept.kept();
        const matches& got = filtered.kept();
        ok = ok && got.size() == want.size() && !want.empty();
        for (std::size_t i = 0; ok && i < got.size(); ++i) {
            ok = got[i].a() == want[i].a() && got[i].b() == want[i].b() && got[i].score().shared() == want[i].score().shared();
        }
    }
    return ok;
}
static_assert(filtered_order_law());

// A token_index is the same however its build is cut into tasks — every row, token and rank a
// task of its own, a few at a time, one bucket or one per token, every count ranked by sorting —
// so the adapter's tasks on every core build the proven index. The family, and three units whose
// repeat of subtree 5 collides with another subtree of theirs: the fingerprint takes the next free
// repeat number, so it stays a set (fingerprint::keep_a_set) and each token occurs once.
consteval bool token_index_build_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(family, "family.cpp"));
    c.add(std::move(src), "family.cpp");
    std::vector<unit> units = c.units();
    const subtree twice = fingerprint::repeat(5, 1);   // {5, 5, twice}: the second 5 becomes `twice`, as the third is
    units.emplace_back(unit_kind::function, "x", 1, fingerprint(0, 4, {5, 5, twice, 9}));
    units.emplace_back(unit_kind::function, "y", 2, fingerprint(0, 4, {5, twice, 9, 11}));
    units.emplace_back(unit_kind::function, "z", 3, fingerprint(0, 3, {5, 5, twice}));
    std::vector<std::uint32_t> order;
    for (std::uint32_t i = 0; i < units.size(); ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return units[a].weight() != units[b].weight() ? units[a].weight() < units[b].weight() : a < b; });
    const std::vector<subtree>& z = units[units.size() - 1].fp().subtrees();
    bool ok = z.size() == 3 && std::adjacent_find(z.begin(), z.end()) == z.end();   // the collision resolved: still a set
    auto grain = [](std::size_t rows, std::size_t tokens, std::size_t ranks, std::size_t bucket, std::size_t counted) {
        return build_grain().with_rows(rows).with_tokens(tokens).with_ranks(ranks).with_bucket(bucket).with_counted(counted);
    };
    for (double t : {0.0, 0.5, 0.8, 1.0}) {
        const token_index whole(units, order, t);
        for (const build_grain& g : {grain(1, 1, 1, 1, 1), grain(2, 3, 2, 1, 2), grain(3, 7, 5, 4, 3), grain(5, 40, 9, 1 << 20, 1)}) {
            ok = ok && token_index(units, order, t, sequential_build(g)) == whole;
        }
        // and what it indexes: every token, the shared counts the merges give; a prefix, every pair reaching t
        std::vector<std::uint32_t> counts(order.size(), 0), seen(order.size(), 0);
        for (std::size_t r = 0; r < order.size(); ++r) {
            std::vector<bool> offered(order.size(), false);
            if (t == 0.0) whole.count_shared(r, counts, order.size());
            else whole.candidates(r, seen, [&](std::size_t j) { offered[j] = true; });
            for (std::size_t j = r + 1; j < order.size(); ++j) {
                const std::uint64_t shared = shared_weight(units[order[r]].fp(), units[order[j]].fp());
                if (t == 0.0) ok = ok && counts[j] == shared;
                else ok = ok && (offered[j] || score(shared, whole.weight(r) + whole.weight(j)).value() < t);
            }
            std::fill(counts.begin(), counts.end(), 0u);
        }
    }
    return ok;
}
static_assert(token_index_build_law());

// A prefix holds a unit's tokens less the overlap any partner needs: at 0.8, ⌈8·0.8/1.2⌉ = 6 of 8
// are needed, so 3 tokens; at 1.0 every token is needed, so 1.
static_assert(token_index::prefix_length(8, 0.8) == 3 && token_index::prefix_length(8, 1.0) == 1 &&
              token_index::prefix_length(1, 0.8) == 1 && token_index::prefix_length(0, 0.8) == 0);

// Tokens are ranked rarest first, so a prefix holds a row's rarest tokens: at a threshold whose
// prefix is one token, three units that share only a common subtree, each with a rare one of its
// own, offer no candidate to each other. Ranked most common first, each would offer the others,
// and prefix_filter_law, true for any global order, would not notice.
consteval bool rarest_first_law() {
    std::vector<unit> us;
    us.emplace_back(unit_kind::function, "a", 1, fingerprint(0, 2, {1, 10}));
    us.emplace_back(unit_kind::function, "b", 2, fingerprint(0, 2, {2, 10}));
    us.emplace_back(unit_kind::function, "c", 3, fingerprint(0, 2, {3, 10}));
    const token_index ix(us, {0, 1, 2}, 1.0);
    std::vector<std::uint32_t> seen(3, 0);
    std::size_t offered = 0;
    for (std::size_t r = 0; r < 3; ++r) ix.candidates(r, seen, [&](std::size_t) { ++offered; });
    return token_index::prefix_length(2, 1.0) == 1 && offered == 0;
}
static_assert(rarest_first_law());

// The filter's bounds are computed with t a little lowered (token_index::rounding_margin): y, four
// of x's six tokens, scores exactly 0.8, and 0.8 * 6 / 1.2 computes as 4.000000000000001. Taken as
// it is, the bound needs 5 tokens shared, leaves x's prefix its two rarest — its own — and the
// pair, which the sweep's `value >= t` keeps, is never offered.
consteval bool rounding_margin_law() {
    std::vector<unit> us;
    us.emplace_back(unit_kind::function, "y", 1, fingerprint(0, 4, {1, 2, 3, 4}));
    us.emplace_back(unit_kind::function, "x", 2, fingerprint(0, 6, {1, 2, 3, 4, 5, 6}));
    const token_index ix(us, {0, 1}, 0.8);
    std::vector<std::uint32_t> seen(2, 0);
    std::size_t offered = 0;
    ix.candidates(0, seen, [&](std::size_t j) { offered += j == 1; });
    return dice(us[0].fp(), us[1].fp()).value() == 0.8 && offered == 1;
}
static_assert(rounding_margin_law());

// Shared counts from an index of every token (sweep_rows::counted_row) give sweep_row()'s pairs —
// the same scores in the same order — at any threshold and with any limit, whose floor rises.
consteval bool counted_row_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(family, "family.cpp"));
    c.add(std::move(src), "family.cpp");
    const auto order = c.sweep_order(unit_kind::function, 0);
    const token_index all(c.units(), order, 0.0);
    bool ok = true;
    for (double t : {0.0, 0.3, 0.5, 0.8, 1.0}) {
        for (std::size_t limit : {std::size_t{0}, std::size_t{1}, std::size_t{4}, std::size_t{100}}) {
            collector swept(t, limit), counted(t, limit);
            std::vector<std::uint32_t> counts(order.size(), 0);
            for (std::size_t r = 0; r < order.size(); ++r) {
                sweep_rows(c).sweep_row(order, r, swept);
                sweep_rows(c).counted_row(all, order, r, counts, counted);
            }
            for (std::uint32_t k : counts) ok = ok && k == 0;   // left zeroes
            const auto x = corpus<lang_a>::finish(std::move(swept)), y = corpus<lang_a>::finish(std::move(counted));
            ok = ok && x.size() == y.size();
            for (std::size_t i = 0; ok && i < x.size(); ++i) ok = x[i].a() == y[i].a() && x[i].b() == y[i].b() && x[i].score().shared() == y[i].score().shared() && x[i].score().total() == y[i].score().total();
        }
    }
    return ok;
}
static_assert(counted_row_law());

// A sweep at threshold 0 written by places (sweep_positions, a stream's tiles): pieces of rows cut
// at every tile size, each counted for its own partners only and written at its place, give
// sweep_row()'s pairs in its order; the counts are left zeroes.
consteval bool placed_sweep_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(family, "family.cpp"));
    c.add(std::move(src), "family.cpp");
    const auto order = c.sweep_order(unit_kind::function, 0);
    const token_index all(c.units(), order, 0.0);
    collector swept(0.0, 0);
    for (std::size_t r = 0; r < order.size(); ++r) sweep_rows(c).sweep_row(order, r, swept);
    const matches& want = swept.kept();
    const sweep_positions places(order.size());
    bool ok = places.pairs() == want.size() && want.size() == 45;
    for (std::size_t tile = 1; tile <= places.pairs() + 1; ++tile) {
        matches got(places.pairs());
        std::vector<std::uint32_t> counts(order.size(), 0);
        for (std::size_t p = 0; p < places.pairs(); p += tile) {
            places.for_each_piece(p, std::min(p + tile, places.pairs()), [&](std::size_t r, std::size_t from, std::size_t end, std::size_t at) {
                all.count_shared(r, counts, from, end);
                for (std::size_t j = from; j < end; ++j, ++at) {
                    const std::uint32_t a = order[r], b = order[j];
                    got[at] = match(a, b, score(counts[j], all.weight(r) + all.weight(j)));
                    counts[j] = 0;
                }
            });
        }
        for (std::uint32_t k : counts) ok = ok && k == 0;
        for (std::size_t i = 0; ok && i < want.size(); ++i) {
            ok = got[i].a() == want[i].a() && got[i].b() == want[i].b() && got[i].score().shared() == want[i].score().shared() &&
                 got[i].score().total() == want[i].score().total();
        }
    }
    return ok;
}
static_assert(placed_sweep_law());

// Classes counted from a class_index (sweep_rows::counted_class_row) give sweep_row()'s pairs — the
// same scores in the same order — at any threshold, with any limit, at every tile size: methods
// shared in any order, a class without methods, a header alone, and G, A's exact copy, so a
// threshold of 1.0 keeps a pair.
constexpr std::string_view shapes = R"(
struct A { int f(int x) { return x + 1; } int g(int y) { return y * 2; } int h() { return 3; } };
struct B { int g(int q) { return q * 2; } int f(int p) { return p + 1; } };
struct C { int f(int x) { return x + 2; } int k(int z) { if (z > 1) { return z; } return 0; } int h() { return 3; } };
struct D { int v; int w; };
struct E { int f(int x) { return x + 1; } int g(int y) { return y * 2; } int h() { return 3; } int i() { return 4; } };
struct F { int n(int a) { return a - 1; } int f(int x) { return x + 1; } };
struct G { int f(int x) { return x + 1; } int g(int y) { return y * 2; } int h() { return 3; } };
)";
consteval bool class_counted_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(shapes, "shapes.cpp"));
    c.add(std::move(src), "shapes.cpp");
    const auto order = c.sweep_order(unit_kind::class_, 0);
    const class_index ix(c.units(), order);
    bool ok = order.size() == 7 && c.pairs(unit_kind::class_, 1.0, 0, 0).size() == 1;   // (A, G)
    for (double t : {0.0, 0.5, 1.0}) {   // all, a floor that rises, exact only: within the default constexpr budget
        for (std::size_t limit : {std::size_t{0}, std::size_t{1}, std::size_t{100}}) {
            const auto want = c.pairs(unit_kind::class_, t, 0, limit);
            for (std::size_t cells : {std::size_t{1}, std::size_t{3}, std::size_t{1} << 16}) {   // a tile a class, a few, one tile
                class_row_scratch scratch(order.size(), cells);
                collector counted(t, limit);
                for (std::size_t r = 0; r < order.size(); ++r) sweep_rows(c).counted_class_row(ix, order, r, scratch, counted);
                ok = ok && scratch.zeroes();   // left zeroes
                const auto got = corpus<lang_a>::finish(std::move(counted));
                ok = ok && got.size() == want.size();
                for (std::size_t i = 0; ok && i < got.size(); ++i) {
                    ok = got[i].a() == want[i].a() && got[i].b() == want[i].b() && got[i].score().shared() == want[i].score().shared() &&
                         got[i].score().total() == want[i].score().total();
                }
            }
        }
    }
    // A, B, E, F and G share methods in any order; at threshold 0 all 21 pairs
    return ok && c.pairs(unit_kind::class_, 0.0, 0, 0).size() == 21;
}
static_assert(class_counted_law());

// A class sweep at threshold 0 written by places, as a stream's tiles write it: pieces of rows cut
// at every tile size, each counted for its own partners only (counted_class_partners) and written
// at its place, give sweep_row()'s pairs in its order, a tile of methods a class or one for all;
// the scratch is left zeroes.
consteval bool placed_class_law() {
    corpus<lang_a> c;
    parsed_source<lang_a> src;
    src = decltype(src)(cpp::parse_cpp(shapes, "shapes.cpp"));
    c.add(std::move(src), "shapes.cpp");
    const auto order = c.sweep_order(unit_kind::class_, 0);
    const class_index ix(c.units(), order);
    collector swept(0.0, 0);
    for (std::size_t r = 0; r < order.size(); ++r) sweep_rows(c).sweep_row(order, r, swept);
    const matches& want = swept.kept();
    const sweep_positions places(order.size());
    bool ok = places.pairs() == want.size() && want.size() == 21;
    for (std::size_t tile = 1; tile <= places.pairs() + 1; ++tile) {
        matches got(places.pairs());
        class_row_scratch scratch(order.size(), tile % 2 == 0 ? 1 : std::size_t{1} << 16);
        for (std::size_t p = 0; p < places.pairs(); p += tile) {
            places.for_each_piece(p, std::min(p + tile, places.pairs()), [&](std::size_t r, std::size_t from, std::size_t end, std::size_t at) {
                sweep_rows(c).counted_class_partners(ix, order, r, from, end, scratch, [] { return 0.0; }, [&](std::size_t j, const score& s) {
                    const std::uint32_t a = order[r], b = order[j];
                    got[at + (j - from)] = match(a, b, s);
                });
            });
        }
        ok = ok && scratch.zeroes();
        for (std::size_t i = 0; ok && i < want.size(); ++i) {
            ok = got[i].a() == want[i].a() && got[i].b() == want[i].b() && got[i].score().shared() == want[i].score().shared() &&
                 got[i].score().total() == want[i].score().total();
        }
    }
    return ok;
}
static_assert(placed_class_law());

}  // namespace
