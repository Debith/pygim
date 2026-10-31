#pragma once
// similarity/core.h — deterministic structural similarity of functions and
// classes, one language at a time.
//
// CORE layer: pybind-free and constexpr throughout, so the laws below are
// proven at compile time (tests/static/similarity_proofs.cpp). A language
// frontend (python_source.h, cpp_source.h; each named to the build by a
// descriptor in adapter/languages/) turns source text into language-neutral
// trees; everything from there on —
// normalisation, fingerprints, scores, the all-pairs sweep — is written once,
// here, and never knows which language it is looking at.
//
// What is measured. A unit (a function, or a class) is a tree of nodes, each
// a `kind` ("If", "Call", "()", "return"), an optional `label` (a name, a
// literal's type) and a `role` that says what the label means:
//
//     bind      the label names a local: a parameter, an assigned name
//     use       the label is a name read; a local if it is bound anywhere
//     attr      the label is kept verbatim: an attribute, a keyword argument
//     constant  the label is a literal's type ("int", "str"), not its value
//     syntax    no label: pure structure
//
// Normalisation renames every local to its order of first appearance ($0,
// $1, ...) and keeps every other label, so `def f(a): return a + n` and
// `def g(b): return b + n` are the same tree, but `... + m` is not: renaming
// a parameter changes nothing, calling a different global does. Every
// subtree then gets a Merkle hash (its node and its children's hashes, in
// order, its size folded in), and a unit's fingerprint is the sorted
// multiset of its subtree hashes — every subtree of two or more nodes, and
// the root. Two units score the Dice coefficient over those multisets:
//
//     score(A, B) = 2 * shared / (weight(A) + weight(B))
//     shared      = Σ over subtrees present in both, min(count)
//     weight(X)   = the number of X's subtrees
//
// so a large shared block counts once per subtree it contains, and a lone
// leaf (a name, a literal type) not at all: unrelated code shares plenty of
// those. 1 means identical up to local names; 0 means not one subtree in
// common.
//
// Why each subtree counts ONCE, not by its size. One edited token changes
// the hash of every subtree enclosing it — and those are the largest ones,
// the root the largest of all. Weighting by size made one edit cost about a
// third of a function's weight: on 600 real stdlib functions with a single
// structural edit each (flip an operator, rename an attribute, drop or
// duplicate a statement), the median score of edit vs original was 0.60-0.64.
// Counting each subtree once raises it to 0.83-0.88, while the 99th
// percentile of 1,000 unrelated pairs stays at 0.10 — twice the separation
// (docs/design/similarity.md, "Why each subtree counts once").
//
// A class is its header (bases, decorators, members that are not functions)
// plus its methods. Its methods are paired with the other class's by the
// maximum-weight assignment (Hungarian algorithm, exact integer weights), and
// the class score is the same Dice ratio over everything: header overlap plus
// matched-method overlap against the total weight of both classes, so an
// unmatched method counts fully against.
//
// A worked example, used in the comments below (Python, through the python
// frontend; the trees are what that frontend emits):
//
//     def f(a):   return a + 1        # A
//     def g(b):   return b + 1        # B: A with the parameter renamed
//     def h(a):   return a + n        # C: A reading a global instead of 1
//
//     score(A, B)  -> exact, 1.0      (one tree after renaming)
//     score(A, C)  -> 0 < s < 1       (the `+ 1` / `+ n` subtrees differ,
//                                      the parameter list agrees; `a` alone
//                                      is a leaf, and leaves never count)
//
// Determinism. Every hash is pygim's own FNV-1a / splitmix64 (utils/hash.h),
// never std::hash and never Python's salted hash(); every sort has a total
// order; scores are integer ratios (`shared`, `total`) and the float is only
// their correctly rounded quotient. Same input, same output — on every run,
// every platform in the build matrix. A tree's shape depends on the
// frontend: the python frontend reads pygim.code's Python 3.14 tree, so its
// scores are the same whatever Python runs pygim. Kinds and labels are
// atoms, the hashes of their texts: only equal texts compare equal.
//
// Languages never meet. A corpus is a `corpus<L>` for one language L, and it
// accepts only a `parsed_source<L>` — what L's frontend returns. There is no
// operation taking two corpora, so a C++ unit cannot be compared with a Python
// one: not rejected at run time, unwritable (proven in similarity_proofs.cpp).
//
// Lifetime and threading: a corpus is a value; it owns its units and copies
// every string it is given. It is not synchronised — one writer, and
// `pairs()` is a const read any number of threads may run on an unchanging
// corpus.

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../utils/hash.h"
#include "../utils/huge_pages.h"
#include "../utils/kept_blocks.h"
#include "../utils/pieces.h"
#include "../utils/sorted_sets.h"
#include "../utils/string_list.h"
#include "../utils/unfilled.h"

namespace pygim::similarity {

// ── trees ─────────────────────────────────────────────────────────────────

/// What a node's label means — see the file comment. The role is part of the
/// node's hash: a bound `x` and an attribute `.x` never match.
enum class role : std::uint8_t { syntax, bind, use, attr, constant };

/// A kind or a label as the core compares it: the hash of its text. Equal texts are one atom, and
/// two texts share one only by a 64-bit collision, as two subtrees may. A frontend that meets one
/// text often (an interned name, a node kind) makes its atom once, and a node holds eight bytes
/// where it held a string: the trees of a corpus are built, hashed and dropped by the million.
///
///     atom("Name") == atom("Name")      atom("Name") != atom("name")      atom() == atom("")
///     atom::joined("Call", ".", "args") == atom("Call.args")
class atom {
public:
    constexpr atom() noexcept = default;
    constexpr explicit atom(std::string_view text) noexcept : m_hash(hash::mix64(hash::fnv1a(text))) {}
    /// The atom of the pieces' concatenation, without making it: FNV-1a continues one stream.
    [[nodiscard]] static constexpr atom joined(std::string_view a, std::string_view b, std::string_view c) noexcept {
        return atom(hash::mix64(hash::fnv1a(c, hash::fnv1a(b, hash::fnv1a(a)))));
    }

    [[nodiscard]] constexpr std::uint64_t hash() const noexcept { return m_hash; }
    friend constexpr bool operator==(atom, atom) noexcept = default;
    friend constexpr auto operator<=>(atom, atom) noexcept = default;

private:
    constexpr explicit atom(std::uint64_t hash) noexcept : m_hash(hash) {}
    std::uint64_t m_hash = hash::mix64(hash::fnv_basis);   // the empty text's
};
static_assert(atom("Name") == atom("Name") && atom("Name") != atom("name") && atom() == atom("") &&
              atom::joined("Call", ".", "args") == atom("Call.args"));

/// One node of a unit's tree, in preorder: a kind, a label and the label's role. Its size is the
/// number of nodes in the subtree it roots (itself included), so its children are found by
/// skipping: the first child is at i + 1, the next at child + child.size(). 24 bytes.
class node {
public:
    constexpr node(atom kind, atom label, enum role r) noexcept : m_kind(kind), m_label(label), m_role(r) {}

    [[nodiscard]] constexpr atom kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr atom label() const noexcept { return m_label; }
    [[nodiscard]] constexpr enum role role() const noexcept { return m_role; }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return m_size; }

private:
    friend class tree_builder;
    friend class tree;
    atom m_kind;
    atom m_label;
    std::uint32_t m_size = 1;
    enum role m_role;
};
static_assert(sizeof(node) == 24 && node(atom("Name"), atom("x"), role::use).size() == 1);

/// A unit's tree: one root (nodes()[0].size() == nodes().size()), made by a tree_builder. Its shape
/// is fixed; a frontend's pass over a finished tree may only relabel a node.
class tree {
public:
    constexpr tree() = default;

    [[nodiscard]] constexpr const std::vector<node>& nodes() const noexcept { return m_nodes; }
    /// Node `i` as (kind, label, role) instead: `std::move` read as `move` -> qname "std::move".
    constexpr void relabel(std::size_t i, atom kind, atom label, enum role r) noexcept {
        m_nodes[i].m_kind = kind;
        m_nodes[i].m_label = label;
        m_nodes[i].m_role = r;
    }

private:
    friend class tree_builder;
    constexpr explicit tree(std::vector<node> nodes) noexcept : m_nodes(std::move(nodes)) {}
    std::vector<node> m_nodes;
};
static_assert(tree().nodes().empty());   // a class unit's body before its frontend fills it

/// Builds a tree in preorder: open() a node, add its children, close() it. A kind and a label are
/// atoms, or texts made into atoms on the way in.
///
///     tree_builder b;
///     b.open("Return");  b.leaf("Name", "a", role::use);  b.close();
///     std::move(b).finish()     // 2 nodes, nodes()[0].size() == 2
///
/// Cost: one node appended per call; close() is O(1).
class tree_builder {
public:
    /// Opens a node whose children follow until the matching close().
    constexpr void open(atom kind, atom label = {}, role r = role::syntax) {
        m_open.push_back(m_nodes.size());
        m_nodes.emplace_back(kind, label, r);
    }
    constexpr void open(std::string_view kind, std::string_view label = {}, role r = role::syntax) { open(atom(kind), atom(label), r); }
    /// A node without children: open() + close().
    constexpr void leaf(atom kind, atom label = {}, role r = role::syntax) { m_nodes.emplace_back(kind, label, r); }
    constexpr void leaf(std::string_view kind, std::string_view label = {}, role r = role::syntax) { leaf(atom(kind), atom(label), r); }
    /// Closes the innermost open node. A close() without an open() is a
    /// frontend bug: std::logic_error.
    constexpr void close() {
        if (m_open.empty()) throw std::logic_error("similarity::tree_builder: close() without a matching open()");
        const std::size_t i = m_open.back();
        m_open.pop_back();
        m_nodes[i].m_size = static_cast<std::uint32_t>(m_nodes.size() - i);
    }
    /// Number of nodes opened and not yet closed.
    [[nodiscard]] constexpr std::size_t depth() const noexcept { return m_open.size(); }
    /// The tree. Exactly one root, every node closed — else std::logic_error.
    [[nodiscard]] constexpr tree finish() && {
        if (!m_open.empty()) throw std::logic_error("similarity::tree_builder: finish() with an unclosed node");
        if (m_nodes.empty() || m_nodes[0].size() != m_nodes.size())
            throw std::logic_error("similarity::tree_builder: a tree has exactly one root");
        return tree(std::move(m_nodes));
    }

private:
    std::vector<node> m_nodes;
    std::vector<std::size_t> m_open;
};
static_assert([] {   // the example above
    tree_builder b;
    b.open("Return");
    b.leaf("Name", "a", role::use);
    b.close();
    const tree t = std::move(b).finish();
    return t.nodes().size() == 2 && t.nodes()[0].size() == 2 && t.nodes()[1].label() == atom("a");
}());

// ── fingerprints ─────────────────────────────────────────────────────────

/// One subtree of a fingerprint: its Merkle hash, which folds in the subtree's node count, so
/// two subtrees of different sizes never share it but by a 64-bit collision. Eight bytes, so a
/// merge compares one integer: the all-pairs sweep is a merge per pair.
using subtree = std::uint64_t;

/// A unit's normalised tree as a multiset of subtrees — every subtree of 2+ nodes, and the root —
/// held as a set: a subtree's first occurrence is its hash, its k-th repeat a mix of (hash, k),
/// all sorted. Two units sharing a subtree twice share two values, so Σ min(count) of the
/// multisets is the size of the sets' intersection (utils/sorted_sets.h). Its weight, the
/// denominator's share of the unit, is how many it holds.
class fingerprint {
public:
    constexpr fingerprint() = default;
    /// The fingerprint of a tree of `nodes` nodes whose root hashes to `root`; `subtrees` in any order.
    constexpr fingerprint(std::uint64_t root, std::uint32_t nodes, std::vector<subtree> subtrees)
        : m_root(root), m_nodes(nodes), m_subtrees(std::move(subtrees)) {
        std::sort(m_subtrees.begin(), m_subtrees.end());
        // the repeats out of the sorted run (it stays sorted), their values sorted alone, merged back
        std::vector<subtree> repeats;
        std::size_t kept = 0;
        for (std::size_t i = 0, first = 0; i < m_subtrees.size(); ++i) {   // first: where the run of i's hash starts
            if (i == 0 || m_subtrees[i] != m_subtrees[first]) {
                first = i;
                m_subtrees[kept++] = m_subtrees[i];
            } else {
                repeats.push_back(repeat(m_subtrees[first], i - first));   // the (i - first)-th repeat
            }
        }
        if (repeats.empty()) return;
        std::sort(repeats.begin(), repeats.end());
        for (std::size_t i = kept, j = repeats.size(), out = m_subtrees.size(); j > 0;) {   // from the back: the larger first
            m_subtrees[--out] = i > 0 && m_subtrees[i - 1] > repeats[j - 1] ? m_subtrees[--i] : repeats[--j];
        }
        keep_a_set();
    }

    [[nodiscard]] constexpr std::uint64_t root() const noexcept { return m_root; }       ///< equal roots, identical units
    [[nodiscard]] constexpr std::uint32_t nodes() const noexcept { return m_nodes; }     ///< leaves included
    [[nodiscard]] constexpr std::uint64_t weight() const noexcept { return m_subtrees.size(); }
    /// The set: ascending, no value twice.
    [[nodiscard]] constexpr const std::vector<subtree>& subtrees() const noexcept { return m_subtrees; }

    /// The value the k-th repeat (k ≥ 1) of the subtree `hash` stands for.
    [[nodiscard]] static constexpr subtree repeat(subtree hash, std::size_t k) noexcept {
        return hash::mix64(hash ^ (static_cast<std::uint64_t>(k) * 0x9e3779b97f4a7c15ull));
    }

private:
    // A repeat's value may equal another subtree's — a 64-bit collision, never seen in practice —
    // and the fingerprint would hold it twice. Each such copy takes the next repeat number of its
    // value that is free, so the fingerprint stays a set and every merge counts it the same: the
    // AVX2 merge counted 4 shared where the scalar one, the gallop and the token index counted 3
    // (found 2026-10-29). Deterministic, as the values alone decide; cold.
    constexpr void keep_a_set() {
        for (auto twice = std::adjacent_find(m_subtrees.begin(), m_subtrees.end()); twice != m_subtrees.end();
             twice = std::adjacent_find(m_subtrees.begin(), m_subtrees.end())) {
            subtree fresh = *twice;
            for (std::size_t k = 1; std::binary_search(m_subtrees.begin(), m_subtrees.end(), fresh); ++k) fresh = repeat(*twice, k);
            *std::next(twice) = fresh;
            std::sort(m_subtrees.begin(), m_subtrees.end());
        }
    }

    std::uint64_t m_root = 0;
    std::uint32_t m_nodes = 0;
    std::vector<subtree> m_subtrees;
};
static_assert(fingerprint(7, 3, {9, 2}).subtrees().front() == 2 && fingerprint(7, 3, {9, 2}).weight() == 2 &&   // what fingerprint_of hands over
              fingerprint(7, 3, {5, 5}).weight() == 2 && fingerprint(7, 3, {5, 5}).subtrees()[0] != fingerprint(7, 3, {5, 5}).subtrees()[1]);
static_assert([] {   // the second 5 stands for repeat(5, 1), which the source holds already: a set still
    const fingerprint f(7, 4, {5, 5, fingerprint::repeat(5, 1), 9});
    return f.weight() == 4 && std::adjacent_find(f.subtrees().begin(), f.subtrees().end()) == f.subtrees().end();
}());

/// The fingerprint of `t`: locals renamed by first appearance, then a Merkle
/// hash per subtree. On the worked example, A and B get the same root and the
/// same subtrees; A and C share every subtree but the ones containing `1`/`n`.
///
/// A label is a local when some node binds it (role::bind); every use of it,
/// before or after the binding, is renamed. A name only read is kept.
///
/// Cost: O(n log n) in the node count (the sorts); one allocation per vector.
[[nodiscard]] constexpr fingerprint fingerprint_of(const tree& t) {
    const std::vector<node>& n = t.nodes();
    // The locals: every label bound anywhere in the unit.
    std::vector<atom> bound;
    for (const node& x : n) {
        if (x.role() == role::bind) bound.push_back(x.label());
    }
    std::sort(bound.begin(), bound.end());
    bound.erase(std::unique(bound.begin(), bound.end()), bound.end());

    // Each node's own hash, locals by their order of first appearance.
    constexpr std::uint32_t unseen = std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint64_t local = 0x6c6f63616c000000ull;   // "local": a renamed label is no text's atom
    std::vector<std::uint32_t> slot(bound.size(), unseen);
    std::uint32_t next = 0;
    std::vector<std::uint64_t> hashes(n.size());   // each node's own hash, then its subtree's
    std::size_t counted = 0;                       // the subtrees a fingerprint keeps
    for (std::size_t i = 0; i < n.size(); ++i) {
        const node& x = n[i];
        const std::uint64_t h = hash::combine64(hash::combine64(hash::fnv_basis, static_cast<std::uint64_t>(x.role())), x.kind().hash());
        std::uint64_t label = x.label().hash();
        if (x.role() == role::bind || x.role() == role::use) {
            const auto it = std::lower_bound(bound.begin(), bound.end(), x.label());
            if (it != bound.end() && *it == x.label()) {
                std::uint32_t& s = slot[static_cast<std::size_t>(it - bound.begin())];
                if (s == unseen) s = next++;
                label = hash::mix64(local + s);
            }
        }
        hashes[i] = hash::combine64(h, label);
        counted += i == 0 || x.size() >= 2;
    }

    // Merkle hashes bottom-up: a node's children come after it in preorder, so theirs are final
    // when its own is folded with them.
    for (std::size_t i = n.size(); i-- > 0;) {
        std::uint64_t h = hashes[i];
        for (std::size_t j = i + 1; j < i + n[i].size(); j += n[j].size()) h = hash::combine64(h, hashes[j]);   // in order: (a, b) is not (b, a)
        hashes[i] = hash::mix64(hash::combine64(h, n[i].size()));
    }

    std::vector<subtree> kept;
    kept.reserve(counted);
    for (std::size_t i = 0; i < n.size(); ++i) {
        if (i == 0 || n[i].size() >= 2) kept.push_back(hashes[i]);
    }
    return fingerprint(n.empty() ? 0 : hashes[0], static_cast<std::uint32_t>(n.size()), std::move(kept));
}
static_assert([] {   // A and B of the worked example: one tree once the parameter is renamed
    auto unit = [](std::string_view param) {
        tree_builder b;
        b.open("FunctionDef");
        b.leaf("arg.arg", param, role::bind);
        b.open("Return");
        b.open("BinOp");
        b.leaf("Name", param, role::use);
        b.leaf("Add");
        b.leaf("Constant", "int", role::constant);
        b.close();
        b.close();
        b.close();
        return fingerprint_of(std::move(b).finish());
    };
    const fingerprint a = unit("a"), b = unit("b");
    return a.root() == b.root() && a.subtrees() == b.subtrees() && a.weight() == 3 && a.nodes() == 7;   // FunctionDef, Return, BinOp
}());

// ── scores ────────────────────────────────────────────────────────────────

/// A Dice score as its exact integer ratio: value = 2 * shared / total.
class score {
public:
    constexpr score() = default;
    constexpr score(std::uint64_t shared, std::uint64_t total) noexcept : m_shared(shared), m_total(total) {}

    [[nodiscard]] constexpr std::uint64_t shared() const noexcept { return m_shared; }   ///< counted subtrees the two units have in common
    [[nodiscard]] constexpr std::uint64_t total() const noexcept { return m_total; }     ///< weight(A) + weight(B)
    /// The score in [0, 1], correctly rounded from the integers (so the same
    /// on every platform). Two empty units are identical: 1.
    [[nodiscard]] constexpr double value() const noexcept {
        return m_total == 0 ? 1.0 : static_cast<double>(2 * m_shared) / static_cast<double>(m_total);
    }
    /// Identical up to local names: everything shared.
    [[nodiscard]] constexpr bool exact() const noexcept { return 2 * m_shared == m_total; }

private:
    std::uint64_t m_shared = 0;
    std::uint64_t m_total = 0;
};
static_assert(score(3, 10).value() == 0.6 && score(5, 10).exact() && score().value() == 1.0);   // a pair's score column

/// Σ min(count) over the subtrees `a` and `b` share: the size of their fingerprints' intersection
/// (utils/sorted_sets.h — a branch-free merge, four values a step with AVX2, galloping when one
/// is more than 16 times the other). Symmetric; shared_weight(a, a) == a.weight.
/// Cost: O(|a| + |b|), or O(small · log large); no allocation.
[[nodiscard]] constexpr std::uint64_t shared_weight(const fingerprint& a, const fingerprint& b) noexcept {
    return sorted_sets::intersection_size(a.subtrees().data(), a.subtrees().size(), b.subtrees().data(), b.subtrees().size());
}
static_assert([] {   // the merge and the gallop count min(count) alike
    std::vector<subtree> twice;
    for (subtree k = 0; k < 100; ++k) twice.push_back(k / 2);   // 0,0,1,1,...,49,49: each twice
    const fingerprint a(0, 0, {1, 2, 2, 5}), b(0, 0, {2, 2, 2, 5, 9}), big(0, 0, std::move(twice));
    return shared_weight(a, b) == 3 && shared_weight(b, a) == 3      // 2 twice, 5 once (merged)
           && shared_weight(a, big) == 4 && shared_weight(big, a) == 4   // 1, 2 twice, 5 (galloped)
           && shared_weight(b, big) == 4;                               // 2 twice (of three), 5, 9
}());

/// The Dice score of two fingerprints. dice(A, B) on the worked example is
/// exact; dice(A, C) is strictly between 0 and 1.
[[nodiscard]] constexpr score dice(const fingerprint& a, const fingerprint& b) noexcept {
    return score(shared_weight(a, b), a.weight() + b.weight());
}
static_assert([] {   // corpus::compare of two functions sharing one subtree of two
    const fingerprint a(0, 0, {1, 2}), b(0, 0, {1, 3});
    const score s = dice(a, b);
    return s.shared() == 1 && s.total() == 4 && s.value() == 0.5;
}());

/// The best score two units of these weights could reach (all of the lighter
/// one shared): what lets the all-pairs sweep skip a pair below the threshold
/// without merging its fingerprints. Never below the real score.
[[nodiscard]] constexpr double ceiling(std::uint64_t wa, std::uint64_t wb) noexcept {
    return score(std::min(wa, wb), wa + wb).value();
}
static_assert(ceiling(10, 30) == 0.5);   // 2·10 / 40: a sweep at t = 0.6 skips this pair unmerged

/// How far a row of weight `w` reaches among partners [from, end) that come lightest first: the
/// first whose ceiling with it falls below `bar`, or `end`. Their ceilings only fall, so every
/// partner from there on falls below too. `weight(j)`: partner j's weight.
template <class Weight>
[[nodiscard]] constexpr std::size_t reach(std::uint64_t w, std::size_t from, std::size_t end, double bar, const Weight& weight) {
    while (from < end) {
        const std::size_t mid = from + (end - from) / 2;
        if (ceiling(w, weight(mid)) < bar) end = mid;
        else from = mid + 1;
    }
    return end;
}
static_assert(reach(10, 0, 3, 0.6, [](std::size_t j) { return std::uint64_t{10} + 10 * j; }) == 2);   // ceilings 1, 0.67, 0.5

namespace detail {

// Defined at namespace scope, before the solver whose members are made of it: nested in the
// solver, its member functions' bodies were parsed only once the solver was complete, after
// its members had instantiated it, and clang then found costs() undefined in the proofs'
// constant evaluation (PR #39, macOS).
/// Shortest augmenting paths, the rows added one at a time (Kuhn-Munkres in e-maxx.ru's form),
/// on the costs -w (k x K, row-major). Every column a row's search reaches is matched but the
/// last, and some column is always free (k ≤ K), its potential still 0; so, for weights under
/// `top`, the potentials of the rows added stay in [-top, 0], the columns' in [-top, 0], and
/// every reduced cost in [-top, 2·top] (the first step of a row's search, from its potential 0,
/// reads costs from -top): I may be 32 bits wide while 2·top < 2^31. assignment_solver's
/// narrow_top, 2^28, keeps a factor of four from that edge; no class's methods share that many
/// subtrees. The scan branches past a reached column; the update of the reduced costs masks it
/// instead: on the 744,955 matrices of CPython's stdlib that reach the algorithm, one thread, 32
/// bits, columns from 0 and the masked update took 416-432 ms where 64 bits, a column 0 standing
/// for the row being added and branches took 472-479 (a branch to skip a reached column's
/// subtraction alone cost 15%; 2026-10-28).
template <class I>
class hungarian {
public:
    [[nodiscard]] constexpr std::vector<I>& costs() noexcept { return m_cost; }

    constexpr std::uint64_t solve(std::size_t n, std::size_t m) {
        constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
        constexpr I unreached = std::numeric_limits<I>::max();
        m_u.assign(n, 0);
        m_v.assign(m, 0);
        m_row_of.assign(m, none);
        m_reduced.resize(m);
        m_from.resize(m);
        for (std::uint32_t added = 0; added < n; ++added) {
            std::fill(m_reduced.begin(), m_reduced.end(), unreached);   // each column's least reduced cost from the rows reached
            m_reached.assign(m, 0);                                       // all ones once reached
            m_tree.clear();
            std::uint32_t i = added, from = none, to = none;   // the row searched from, the column that reached it
            for (;;) {
                const I* row = m_cost.data() + std::size_t{i} * m;
                const I ui = m_u[i];
                I delta = unreached;
                for (std::uint32_t j = 0; j < m; ++j) {
                    if (m_reached[j]) continue;
                    const I reduced = row[j] - ui - m_v[j];
                    if (reduced < m_reduced[j]) {
                        m_reduced[j] = reduced;
                        m_from[j] = from;
                    }
                    if (m_reduced[j] < delta) {
                        delta = m_reduced[j];
                        to = j;
                    }
                }
                // the potentials: the rows reached up by delta, their columns down; the rest's reduced costs down
                m_u[added] += delta;
                for (std::uint32_t j : m_tree) {
                    m_u[m_row_of[j]] += delta;
                    m_v[j] -= delta;
                }
                for (std::uint32_t j = 0; j < m; ++j) m_reduced[j] -= delta & ~m_reached[j];
                m_reached[to] = ~I{0};
                m_tree.push_back(to);
                if (m_row_of[to] == none) break;   // a free column: the path ends
                i = m_row_of[to];
                from = to;
            }
            for (std::uint32_t j = to; j != none;) {   // along the path, each column takes the row of the one before it
                const std::uint32_t before = m_from[j];
                m_row_of[j] = before == none ? added : m_row_of[before];
                j = before;
            }
        }
        std::uint64_t total = 0;
        for (std::size_t j = 0; j < m; ++j) {
            if (m_row_of[j] != none) total += static_cast<std::uint64_t>(-m_cost[std::size_t{m_row_of[j]} * m + j]);
        }
        return total;
    }

private:
    std::vector<I> m_cost, m_u, m_v, m_reduced, m_reached;
    std::vector<std::uint32_t> m_row_of, m_from, m_tree;
};

}  // namespace detail

/// The maximum-weight assignment of `rows` to `cols` (w is row-major,
/// rows x cols): pairs every element of the smaller side with a distinct one
/// of the larger, maximising Σ w, and returns that sum. The Hungarian
/// algorithm on exact integers, so the optimum — and the value — is the same
/// on every run.
///
///     w = {5, 4,
///          4, 1}    -> 8   (4 + 4, where taking the 5 first leaves 1: 6)
///
/// Before the algorithm, the bound every assignment stays under: each element of the smaller side
/// with its best partner. Where those partners are all distinct, that assignment exists and is the
/// best — one method each, or methods that each match one other best, as most classes' are — and
/// where the bound is 0, nothing is shared. Either way no Hungarian step runs (it was 73% of a
/// counted class sweep). An element that shares nothing with any partner takes whichever is left
/// over (k ≤ K), so only the elements that share something need distinct partners.
///
/// The weights come through an accessor, w(r, c), so a class row hands its tile of counts over
/// without copying a matrix a pair; they are read once, into the algorithm's orientation.
///
/// Cost: O(k K) when the bound is reached, else O(k² K), for k = min(rows, cols), K = max(rows, cols).
class assignment_solver {
public:
    /// What no assignment of w exceeds: each element of the smaller side with its best partner.
    template <class Weight>
    [[nodiscard]] static constexpr std::uint64_t bound(std::size_t rows, std::size_t cols, const Weight& w) noexcept {
        std::uint64_t by_rows = 0, by_cols = 0;
        for (std::size_t r = 0; r < rows; ++r) {
            std::uint64_t best = 0;
            for (std::size_t c = 0; c < cols; ++c) best = std::max<std::uint64_t>(best, w(r, c));
            by_rows += best;
        }
        for (std::size_t c = 0; c < cols; ++c) {
            std::uint64_t best = 0;
            for (std::size_t r = 0; r < rows; ++r) best = std::max<std::uint64_t>(best, w(r, c));
            by_cols += best;
        }
        return std::min(by_rows, by_cols);
    }
    [[nodiscard]] static constexpr std::uint64_t bound(const std::vector<std::uint64_t>& w, std::size_t rows, std::size_t cols) noexcept {
        return bound(rows, cols, [&](std::size_t r, std::size_t c) { return w[r * cols + c]; });
    }

    /// The best assignment's sum, as best_assignment(); the buffers stay for the next matrix.
    template <class Weight>
    [[nodiscard]] constexpr std::uint64_t best(std::size_t rows, std::size_t cols, const Weight& w) {
        if (rows == 0 || cols == 0) return 0;
        const bool flip = rows > cols;
        const std::size_t n = flip ? cols : rows;   // the smaller side: the algorithm's rows
        const std::size_t m = flip ? rows : cols;
        auto at = [&](std::size_t i, std::size_t j) -> std::uint64_t { return flip ? w(j, i) : w(i, j); };
        // one pass: the costs in that orientation (32 bits while every weight is under narrow_top),
        // and each row's best partner
        m_narrow.costs().resize(n * m);
        m_taken.assign(m, 0);
        std::uint64_t top = 0, bound = 0;
        bool distinct = true;
        for (std::size_t i = 0; i < n; ++i) {
            std::int32_t* row = m_narrow.costs().data() + i * m;
            std::size_t best = 0;
            std::uint64_t most = 0;
            for (std::size_t j = 0; j < m; ++j) {
                const std::uint64_t x = at(i, j);
                row[j] = -static_cast<std::int32_t>(x & (narrow_top - 1));
                if (x > most) most = x, best = j;
            }
            top = std::max(top, most);
            bound += most;
            if (most > 0) {   // a row sharing nothing takes any column left over
                distinct = distinct && m_taken[best] == 0;
                m_taken[best] = 1;
            }
        }
        if (bound == 0 || distinct) return bound;
        if (top < narrow_top) return m_narrow.solve(n, m);
        m_wide.costs().resize(n * m);   // a weight of 2^28 or more: the costs again, in 64 bits
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < m; ++j) m_wide.costs()[i * m + j] = -static_cast<std::int64_t>(at(i, j));
        }
        return m_wide.solve(n, m);
    }
    [[nodiscard]] constexpr std::uint64_t best(const std::vector<std::uint64_t>& w, std::size_t rows, std::size_t cols) {
        return best(rows, cols, [&](std::size_t r, std::size_t c) { return w[r * cols + c]; });
    }

private:
    static constexpr std::uint64_t narrow_top = std::uint64_t{1} << 28;

    std::vector<std::uint8_t> m_taken;
    detail::hungarian<std::int32_t> m_narrow;
    detail::hungarian<std::int64_t> m_wide;
};

static_assert([] {   // the example above: its bound is 9, and the algorithm finds 8
    assignment_solver s;
    return assignment_solver::bound({5, 4, 4, 1}, 2, 2) == 9 && s.best({5, 4, 4, 1}, 2, 2) == 8;
}());
static_assert([] {   // two rows sharing nothing take the columns the third leaves: the bound, 4
    assignment_solver s;
    return s.best({0, 0, 0, 0, 0, 0, 4, 0, 0}, 3, 3) == 4;
}());

/// The best assignment's sum with a solver of its own: what a single comparison asks.
[[nodiscard]] constexpr std::uint64_t best_assignment(const std::vector<std::uint64_t>& w, std::size_t rows, std::size_t cols) {
    assignment_solver solver;
    return solver.best(w, rows, cols);
}
static_assert(best_assignment({5, 4, 4, 1}, 2, 2) == 8);   // two classes' methods paired

// ── languages ─────────────────────────────────────────────────────────────

/// The pybind-free face of one language: its name, a one-sentence doc, and
/// the file extensions it owns (lower-case, leading dot). Every language
/// header declares one as `static constexpr language_info info`.
class language_info {
public:
    constexpr language_info(std::string_view name, std::string_view doc, string_list exts) noexcept : m_name(name), m_doc(doc), m_exts(exts) {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }   ///< language="cpp"
    [[nodiscard]] constexpr std::string_view doc() const noexcept { return m_doc; }     ///< one sentence: what the frontend parses with
    [[nodiscard]] constexpr string_list exts() const noexcept { return m_exts; }        ///< a file belongs to exactly one language
    [[nodiscard]] constexpr bool owns(std::string_view ext) const noexcept { return m_exts.contains(ext); }

private:
    std::string_view m_name;
    std::string_view m_doc;
    string_list m_exts;
};
static_assert(!language_info("none", "", string_list()).owns(".py"));   // Languages::owner_of asks each in turn

/// A language tag: anything with a `language_info`. (The adapter's
/// `Language` concept adds the frontend, `parse`.)
template <class L>
concept language = requires {
    { L::info } -> std::convertible_to<const language_info&>;
};

namespace detail {
// A language with no frontend: units built by hand, for the examples under the corpus's types.
class example_language {
public:
    static constexpr std::array<std::string_view, 1> exts{".example"};
    static constexpr language_info info{"example", "units built by hand", exts};
};
}  // namespace detail
static_assert(language<detail::example_language>);

/// No extension is owned by two of `Ls`, and no two share a name — so a file
/// routes to at most one language. The registry asserts it on the real pack.
template <language... Ls>
[[nodiscard]] consteval bool languages_disjoint() {
    constexpr std::array<const language_info*, sizeof...(Ls)> all{&Ls::info...};
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            if (all[i]->name() == all[j]->name()) return false;
            for (std::string_view x : all[i]->exts()) {
                if (all[j]->owns(x)) return false;
            }
        }
    }
    return true;
}

static_assert(languages_disjoint<detail::example_language>());   // the registry's check, on a pack of one

// ── units and the corpus ─────────────────────────────────────────────────

/// What a unit is. Functions are compared with functions (methods included:
/// a method is a function), classes with classes; never across.
enum class unit_kind : std::uint8_t { function, class_ };

/// Every kind's name, by its number (number_of): what results show and queries take.
inline constexpr std::array<std::string_view, 2> unit_kind_names{"function", "class"};
/// A kind's number: its index in unit_kind_names, and in any table of kinds.
[[nodiscard]] constexpr std::size_t number_of(unit_kind k) noexcept { return static_cast<std::size_t>(k); }
/// The kind named `name` ("function", "class"), or none.
[[nodiscard]] constexpr std::optional<unit_kind> unit_kind_named(std::string_view name) noexcept {
    for (std::size_t k = 0; k < unit_kind_names.size(); ++k) {
        if (unit_kind_names[k] == name) return static_cast<unit_kind>(k);
    }
    return std::nullopt;
}
static_assert(unit_kind_names[number_of(unit_kind::class_)] == "class" && unit_kind_named("function") == unit_kind::function &&
              !unit_kind_named("method"));

/// One unit as a frontend produced it. For a class, `body` is its header
/// tree and `methods` index its methods in the same parsed_source.
class parsed_unit {
public:
    constexpr parsed_unit(unit_kind kind, std::string qualname, std::uint32_t line, tree body = {}, std::vector<std::uint32_t> methods = {})
        : m_kind(kind), m_qualname(std::move(qualname)), m_line(line), m_body(std::move(body)), m_methods(std::move(methods)) {}

    [[nodiscard]] constexpr unit_kind kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr const std::string& qualname() const noexcept { return m_qualname; }
    [[nodiscard]] constexpr std::uint32_t line() const noexcept { return m_line; }
    [[nodiscard]] constexpr const tree& body() const noexcept { return m_body; }
    [[nodiscard]] constexpr const std::vector<std::uint32_t>& methods() const noexcept { return m_methods; }

    /// A class's header, once its frontend has read it (its methods are units of their own).
    constexpr void set_body(tree body) noexcept { m_body = std::move(body); }
    /// A method of this class: unit `index` of the same source.
    constexpr void add_method(std::uint32_t index) { m_methods.push_back(index); }

private:
    friend class unit;   // made from a parsed unit, taking its name and methods
    unit_kind m_kind;
    std::string m_qualname;
    std::uint32_t m_line;
    tree m_body;
    std::vector<std::uint32_t> m_methods;
};
static_assert([] {   // a class with its header read and one method, unit 1 of its source
    parsed_unit c(unit_kind::class_, "K", 3);
    c.add_method(1);
    return c.qualname() == "K" && c.methods() == std::vector<std::uint32_t>{1} && c.body().nodes().empty();
}());

/// Where a source text comes from, as a frontend needs it: `origin` names it
/// in errors and results; `module` is its dotted module name and `package`
/// the package its relative imports start from (names.h). Both may be empty
/// (unknown): references then resolve only as far as the text itself says.
class source_context {
public:
    constexpr source_context(std::string_view origin = "<string>", std::string_view module = {}, std::string_view package = {}) noexcept
        : m_origin(origin), m_module(module), m_package(package) {}
    /// It views its texts: a temporary string's would be gone before the parse reads it.
    template <class S>
        requires std::same_as<S, std::string>
    source_context(S&&, std::string_view = {}, std::string_view = {}) = delete;

    [[nodiscard]] constexpr std::string_view origin() const noexcept { return m_origin; }
    [[nodiscard]] constexpr std::string_view module() const noexcept { return m_module; }
    [[nodiscard]] constexpr std::string_view package() const noexcept { return m_package; }

private:
    std::string_view m_origin;
    std::string_view m_module;
    std::string_view m_package;
};
static_assert(source_context().origin() == "<string>" && source_context("m.py", "pkg.m", "pkg").package() == "pkg" &&   // add(text) / add_file
              !std::is_constructible_v<source_context, std::string> && std::is_constructible_v<source_context, const std::string&>);

/// Everything one source text holds, tagged with the language that parsed
/// it: only a corpus<L> accepts a parsed_source<L>.
template <language L>
class parsed_source {
public:
    constexpr parsed_source() = default;
    constexpr explicit parsed_source(std::vector<parsed_unit> units) noexcept : m_units(std::move(units)) {}

    [[nodiscard]] constexpr const std::vector<parsed_unit>& units() const noexcept { return m_units; }
    constexpr void add(parsed_unit u) { m_units.push_back(std::move(u)); }
    /// The units, moved out: what prepared_source fingerprints.
    [[nodiscard]] constexpr std::vector<parsed_unit> release() && noexcept { return std::move(m_units); }

private:
    std::vector<parsed_unit> m_units;
};
static_assert([] {   // what a frontend returns, and what prepared_source takes from it
    parsed_source<detail::example_language> src;
    src.add(parsed_unit(unit_kind::function, "f", 1));
    return src.units().size() == 1 && std::move(src).release()[0].qualname() == "f";
}());

/// The bytes a string holds beyond itself: none while its text fits the buffer inside it.
[[nodiscard]] constexpr std::size_t string_heap_bytes(const std::string& s) noexcept {
    return s.capacity() > std::string().capacity() ? s.capacity() + 1 : 0;
}
static_assert(string_heap_bytes("") == 0);

template <language L>
class prepared_source;
template <language L>
class corpus;
template <language L>
class sweep_rows;

/// A unit inside a corpus: its fingerprint and where it came from.
class unit {
public:
    /// A unit fingerprinted from `p` (its tree is read, its name and methods taken). Methods still
    /// index p's source: a corpus places them.
    constexpr explicit unit(parsed_unit&& p)
        : m_kind(p.m_kind), m_qualname(std::move(p.m_qualname)), m_line(p.m_line), m_fp(fingerprint_of(p.m_body)),
          m_weight(m_fp.weight()), m_methods(std::move(p.m_methods)) {}
    /// A unit with a fingerprint made by hand (examples, proofs).
    constexpr unit(unit_kind kind, std::string qualname, std::uint32_t line, fingerprint fp)
        : m_kind(kind), m_qualname(std::move(qualname)), m_line(line), m_fp(std::move(fp)), m_weight(m_fp.weight()) {}

    [[nodiscard]] constexpr unit_kind kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr const std::string& qualname() const noexcept { return m_qualname; }
    [[nodiscard]] constexpr std::uint32_t origin() const noexcept { return m_origin; }   ///< index into corpus::origins()
    [[nodiscard]] constexpr std::uint32_t line() const noexcept { return m_line; }
    [[nodiscard]] constexpr const fingerprint& fp() const noexcept { return m_fp; }     ///< the function's tree, or the class's header
    [[nodiscard]] constexpr std::uint64_t weight() const noexcept { return m_weight; }  ///< fp's, plus every method's for a class
    [[nodiscard]] constexpr const std::vector<std::uint32_t>& methods() const noexcept { return m_methods; }   ///< indices of a class's methods

    /// The bytes it holds beyond itself (a short name's buffer is inside it).
    [[nodiscard]] constexpr std::size_t heap_bytes() const noexcept {
        return string_heap_bytes(m_qualname) + m_fp.subtrees().capacity() * sizeof(subtree) + m_methods.capacity() * sizeof(std::uint32_t);
    }

private:
    template <language>
    friend class prepared_source;   // counts a class's methods, once
    template <language>
    friend class corpus;            // places it, once

    /// A class's weight: its header's and its methods' (`units`: those its methods index).
    constexpr void count_methods(const std::vector<unit>& units) noexcept {
        for (std::uint32_t m : m_methods) m_weight += units[m].m_fp.weight();
    }
    /// Placed in a corpus: from origin `origin`, its methods `first` further on.
    constexpr void place(std::uint32_t origin, std::uint32_t first) noexcept {
        m_origin = origin;
        for (std::uint32_t& m : m_methods) m += first;
    }

    unit_kind m_kind;
    std::string m_qualname;
    std::uint32_t m_origin = 0;
    std::uint32_t m_line;
    fingerprint m_fp;
    std::uint64_t m_weight;
    std::vector<std::uint32_t> m_methods;
};
static_assert(unit(unit_kind::function, "f", 1, fingerprint(0, 3, {1, 2})).weight() == 2 &&   // token_index reads weights
              unit(unit_kind::function, "f", 1, fingerprint(0, 3, {1, 2})).heap_bytes() == 2 * sizeof(subtree));

/// A source's units fingerprinted, not yet in a corpus: what a worker hands over, so the
/// fingerprints — most of a file's cost after its parse — are computed off the corpus's lock and
/// in parallel. Methods index the source's own units; a corpus offsets them as it adds.
template <language L>
class prepared_source {
public:
    /// `src`'s units with their fingerprints and weights (a class's own plus its methods'). Pure:
    /// any thread may prepare any source. A class's method that is no function of `src` is a
    /// frontend bug: std::logic_error.
    /// Cost: one fingerprint per unit, O(n log n) in its nodes.
    constexpr explicit prepared_source(parsed_source<L> src) {
        std::vector<parsed_unit> parsed = std::move(src).release();
        m_units.reserve(parsed.size());
        for (parsed_unit& p : parsed) {
            for (std::uint32_t m : p.methods()) {
                if (m >= parsed.size() || parsed[m].kind() != unit_kind::function)
                    throw std::logic_error("similarity::corpus: a class's method must index a function of the same source");
            }
            m_units.emplace_back(std::move(p));
        }
        for (unit& u : m_units) u.count_methods(m_units);
    }

    [[nodiscard]] constexpr const std::vector<unit>& units() const noexcept { return m_units; }
    /// The units, moved out: what a corpus adds.
    [[nodiscard]] constexpr std::vector<unit> release() && noexcept { return std::move(m_units); }

private:
    std::vector<unit> m_units;
};
static_assert([] {   // what a worker hands corpus::add: a function's fingerprint and weight
    parsed_source<detail::example_language> src;
    tree_builder b;
    b.open("Return");
    b.leaf("Name", "a", role::use);
    b.close();
    src.add(parsed_unit(unit_kind::function, "f", 1, std::move(b).finish()));
    const prepared_source<detail::example_language> ready(std::move(src));
    return ready.units().size() == 1 && ready.units()[0].qualname() == "f" && ready.units()[0].weight() == 1;   // the root only
}());

/// One scored pair of units (corpus indices), the lower one a(): made of the two in either order.
/// Its value caches the score's: ranking 100M pairs compares it a few billion times. 24 bytes: its
/// counts are 32-bit, as a corpus holds at most 2^32 - 1 subtrees (corpus::max_subtrees), so a
/// ranked table of Lib/test's 62M class pairs holds 1.5 GB of them, not 2.
class match {
public:
    constexpr match() = default;
    constexpr match(std::uint32_t x, std::uint32_t y, class score s) noexcept
        : m_a(std::min(x, y)), m_b(std::max(x, y)), m_shared(static_cast<std::uint32_t>(s.shared())),
          m_total(static_cast<std::uint32_t>(s.total())), m_value(s.value()) {}

    [[nodiscard]] constexpr std::uint32_t a() const noexcept { return m_a; }
    [[nodiscard]] constexpr std::uint32_t b() const noexcept { return m_b; }
    [[nodiscard]] constexpr class score score() const noexcept { return {m_shared, m_total}; }
    [[nodiscard]] constexpr double value() const noexcept { return m_value; }

private:
    std::uint32_t m_a = 0;
    std::uint32_t m_b = 0;
    std::uint32_t m_shared = 0;
    std::uint32_t m_total = 0;
    double m_value = 0.0;
};
static_assert(sizeof(match) == 24 && match(1, 2, score(1, 4)).value() == 0.5 && match(1, 2, score(1, 4)).score().shared() == 1 &&
              match(7, 3, score()).a() == 3);

/// The order pairs are reported in: best score first, then by (a, b). A
/// total order — no two distinct pairs tie — so any partition of a sweep,
/// sorted or cut to its best N, gives the same list.
[[nodiscard]] constexpr bool before(const match& l, const match& r) noexcept {
    if (l.value() != r.value()) return l.value() > r.value();
    return l.a() != r.a() ? l.a() < r.a() : l.b() < r.b();
}
static_assert(before(match(3, 4, score(2, 4)), match(0, 1, score(1, 4))) &&     // the better score first
              before(match(0, 2, score(1, 4)), match(1, 2, score(1, 4))));    // then the lower a

/// Pairs as a collector keeps them and a sweep returns them: a megabyte of them or more lives in a
/// kept block (utils/kept_blocks.h), so the next query's sweep finds its memory faulted in.
using matches = kept_vector<match>;

/// Collects a sweep's pairs: those reaching `threshold`, and with a `limit`
/// only the best `limit` of them (by before()).
///
/// With a limit the admission bar rises as the collector fills: once it
/// holds `limit` pairs, a pair scoring below the worst of them can never be
/// reported, so `floor()` becomes that score and the sweep's ceiling cut
/// ends rows against it — most comparisons of a top-N query are never made.
/// A pair scoring exactly the floor is still admitted (it may win its tie by
/// index). Each thread of a parallel sweep owns one collector; the union of
/// their `kept`, cut to `limit` and sorted, is exactly the sequential result.
///
/// Cost: amortised O(1) per pair; a trim (every `limit` admissions) is
/// O(limit) by nth_element.
class collector {
public:
    /// `limit` 0: keep every pair reaching the threshold.
    constexpr explicit collector(double threshold = 0.0, std::size_t limit = 0) noexcept : m_threshold(threshold), m_limit(limit) {}

    /// The score a pair must reach to be kept.
    [[nodiscard]] constexpr double floor() const noexcept { return m_floor > m_threshold ? m_floor : m_threshold; }

    constexpr void add(const match& m) {
        m_kept.push_back(m);
        if (m_limit != 0 && m_kept.size() >= 2 * m_limit) trim();
    }
    /// Room for `pairs` more, made once (a sweep block at threshold 0 knows its count exactly).
    constexpr void reserve(std::size_t pairs) {
        m_kept.reserve(m_kept.size() + pairs);
        if !consteval {   // megabytes written once: on huge pages (utils/huge_pages.h)
            prefer_huge_pages(m_kept.data(), m_kept.capacity() * sizeof(match));
        }
    }
    /// Another collector's pairs too: the union of a parallel sweep's collectors is the sequential one's.
    constexpr void merge(collector&& other) { m_kept.insert(m_kept.end(), other.m_kept.begin(), other.m_kept.end()); }

    /// The pairs kept so far, in the order they came (a stream's block writes them unranked).
    [[nodiscard]] constexpr const matches& kept() const noexcept { return m_kept; }
    /// Its pairs as pairs() returns them: cut to the limit, ranked by before().
    [[nodiscard]] constexpr matches ranked() && {
        trim();
        std::sort(m_kept.begin(), m_kept.end(), before);
        return std::move(m_kept);
    }

private:
    /// Cuts the pairs to the best `limit` (unsorted) and raises the floor.
    constexpr void trim() {
        if (m_limit == 0 || m_kept.size() <= m_limit) return;
        std::nth_element(m_kept.begin(), m_kept.begin() + static_cast<std::ptrdiff_t>(m_limit - 1), m_kept.end(), before);
        const double worst = m_kept[m_limit - 1].value();
        m_kept.resize(m_limit);
        if (worst > m_floor) m_floor = worst;
    }

    double m_threshold;
    std::size_t m_limit;
    matches m_kept;
    double m_floor = 0.0;
};
static_assert([] {   // a top-1 query: the floor rises to the best pair kept
    collector c(0.0, 1);
    c.add(match(0, 1, score(1, 4)));
    c.add(match(0, 2, score(2, 4)));
    const double floor = c.floor();
    const matches best = std::move(c).ranked();
    return floor == 1.0 && best.size() == 1 && best[0].b() == 2;
}());

/// One value of a sweep row's fingerprint, at its place: what a token_index sorts, by (hash,
/// place) — a total order, so any correct sort gives one result. A token is a fingerprint's value:
/// a fingerprint is a set (a repeated subtree holds a value of its own), so one unit holds a token
/// once. Places number the rows' tokens row after row, so one token's entries in place order are
/// its rows in order. Trivially constructible: a buffer of them is written, never zeroed.
class token_entry {
public:
    token_entry() = default;
    constexpr token_entry(subtree hash, std::uint32_t place) noexcept : m_hash(hash), m_place(place) {}

    [[nodiscard]] constexpr subtree hash() const noexcept { return m_hash; }
    [[nodiscard]] constexpr std::uint32_t place() const noexcept { return m_place; }

    [[nodiscard]] constexpr bool operator<(const token_entry& o) const noexcept { return m_hash != o.m_hash ? m_hash < o.m_hash : m_place < o.m_place; }
    [[nodiscard]] constexpr bool same_token(const token_entry& o) const noexcept { return m_hash == o.m_hash; }

private:
    subtree m_hash;
    std::uint32_t m_place;
};
static_assert(std::is_trivially_default_constructible_v<token_entry> && sizeof(token_entry) == 16 && token_entry(1, 5) < token_entry(1, 6) &&
              token_entry(1, 9) < token_entry(2, 0) && token_entry(1, 5).same_token(token_entry(1, 9)) && !token_entry(1, 5).same_token(token_entry(2, 5)));

/// How finely a token_index's build is cut into tasks: rows a task, tokens a task, ranks a task,
/// the tokens a bucket of the sort's first pass aims at, and the count from which a token's rank is
/// sorted rather than counted. Any cut builds the same index (token_index_build_law). The default
/// gives 16 cores 50 to 1,000 tasks a step on LLVM's 140,686 functions (3.1M tokens). A bucket of
/// 2,048 tokens fills a 32 KB L1, but the sort opens at most 1,024 buckets, so that a task's open
/// buckets stay in its L2: LLVM's tokens land about 3,000 to a bucket (48 KB).
///
///     build_grain().with_rows(2).with_bucket(1)     // the default but for those two; 0 is taken as 1
class build_grain {
public:
    constexpr build_grain() noexcept = default;

    [[nodiscard]] constexpr std::size_t rows() const noexcept { return m_rows; }
    [[nodiscard]] constexpr std::size_t tokens() const noexcept { return m_tokens; }
    [[nodiscard]] constexpr std::size_t ranks() const noexcept { return m_ranks; }
    [[nodiscard]] constexpr std::size_t bucket() const noexcept { return m_bucket; }
    [[nodiscard]] constexpr std::size_t counted() const noexcept { return m_counted; }

    [[nodiscard]] constexpr build_grain with_rows(std::size_t n) const noexcept { return with(&build_grain::m_rows, n); }
    [[nodiscard]] constexpr build_grain with_tokens(std::size_t n) const noexcept { return with(&build_grain::m_tokens, n); }
    [[nodiscard]] constexpr build_grain with_ranks(std::size_t n) const noexcept { return with(&build_grain::m_ranks, n); }
    [[nodiscard]] constexpr build_grain with_bucket(std::size_t n) const noexcept { return with(&build_grain::m_bucket, n); }
    /// At most 2^32 - 1: a token's rows are counted in 32 bits.
    [[nodiscard]] constexpr build_grain with_counted(std::size_t n) const noexcept {
        return with(&build_grain::m_counted, std::min<std::size_t>(n, std::numeric_limits<std::uint32_t>::max()));
    }

private:
    [[nodiscard]] constexpr build_grain with(std::size_t build_grain::*field, std::size_t n) const noexcept {
        build_grain g = *this;
        g.*field = std::max<std::size_t>(n, 1);
        return g;
    }

    std::size_t m_rows = 1024;
    std::size_t m_tokens = std::size_t{1} << 16;
    std::size_t m_ranks = 4096;
    std::size_t m_bucket = 2048;
    std::size_t m_counted = 1024;
};
static_assert(build_grain().rows() == 1024 && build_grain().with_rows(2).with_bucket(0).bucket() == 1 &&   // a grain of nothing is a grain of one
              build_grain().with_rows(2).rows() == 2 && build_grain().with_rows(2).tokens() == build_grain().tokens());

/// How a token_index is built: its steps' tasks, cut by a build_grain, run in turn — what constant
/// evaluation can run. A sweep runs the same tasks on every core (similarity/sweeps.h,
/// parallel_build); they touch disjoint data, so the index is the same however they run.
class sequential_build {
public:
    constexpr explicit sequential_build(build_grain grain = {}) noexcept : m_grain(grain) {}

    [[nodiscard]] constexpr const build_grain& grain() const noexcept { return m_grain; }
    /// f(task) for every task in [0, tasks); the tasks touch disjoint data.
    template <class F>
    constexpr void for_each(std::size_t tasks, F&& f) const {
        for (std::size_t k = 0; k < tasks; ++k) f(k);
    }

private:
    build_grain m_grain;
};
static_assert([] {
    std::size_t sum = 0;
    sequential_build(build_grain().with_ranks(3)).for_each(4, [&](std::size_t k) { sum += k; });
    return sum == 6 && sequential_build(build_grain().with_ranks(3)).grain().ranks() == 3;
}());

/// Every unit's subtree occurrences as tokens, with posting lists of the rows holding each — for
/// two jobs. With a threshold t > 0 only each row's prefix is indexed, and candidates() gives the
/// partners a prefix filter leaves; with t = 0 every token is, and count_shared() gives a row's
/// shared count with every later row at once (sweep_rows::counted_row).
///
/// The partners a fixed threshold leaves to each sweep row, found through a prefix filter — the
/// exact set-similarity join of Bayardo, Ma and Srikant ("Scaling up all pairs similarity search",
/// WWW 2007) — instead of every heavier unit the ceiling lets through.
///
/// A unit is its fingerprint, a set (a subtree twice is two values, so Dice's Σ min(count) is the
/// overlap of those sets). Every token is ranked by how many units hold it,
/// rarest first. If Dice(x, y) = 2·|x ∩ y| / (|x| + |y|) >= t then, as |y| >= t·|x| / (2 - t) for
/// any pair that can reach t, |x ∩ y| >= t·|x| / (2 - t): so x's first |x| - ⌈t·|x| / (2 - t)⌉ + 1
/// tokens and y's first such tokens share one. Rows whose prefixes share no token cannot reach t
/// and are never compared; the rest are compared exactly. The pairs found are therefore exactly
/// the sweep's (prefix_filter_law); measured on CPython's Lib/test at t = 0.8 and 30 nodes, the
/// ranked call fell from 9.7 s to 0.24 s (docs/design/similarity.md).
///
/// The bounds are computed with t a little lowered, so a pair whose exact ratio rounds up to t —
/// which the sweep's `value >= t` admits — is never filtered out.
///
/// The build is five steps, each cut into tasks that touch disjoint data (sequential_build,
/// parallel_build) and none walking every token alone: the rows measured; the tokens written
/// straight into buckets of their hash's top bits and each bucket sorted; the groups of equal
/// tokens ranked by a stable counting sort, each token's rank written at its place; each row's
/// prefix cut from its ranks; each rank's rows gathered from its group. On LLVM's 140,686
/// functions (3.1M tokens) and 16 threads it took 44-57 ms at t = 0.8 and 51-56 ms at t = 0, where
/// sorting on every core and the rest in one thread took 79-114 and 124-144 ms (2026-10-28).
///
/// Rows, places and ranks are 32-bit: an index holds at most 2^32 - 1 tokens, every row's weight
/// together (std::length_error past that; LLVM's functions hold 3.1M).
///
/// Cost: O(T log B) for T tokens (the sum of the weights) in buckets of B; a row's candidates cost
/// the posting lists of its prefix's tokens.
class token_index {
public:
    /// The index of the units of `order` for threshold `t`, built in `build`'s tasks. candidates()
    /// needs a sweep order (weights ascending): its ceiling cut reads the rows' weights in turn.
    /// count_shared() takes the rows in any order — class_index's methods, class by class.
    template <class Build = sequential_build>
    constexpr token_index(const std::vector<unit>& units, const std::vector<std::uint32_t>& order, double t, const Build& build = Build{})
        : m_threshold(t) {
        const std::vector<std::uint32_t> place = measure(units, order, t - rounding_margin, build);   // the filter's bounds only: comparisons use t itself
        const std::size_t total = place.back();
        const unfilled_vector<token_entry> tokens = sorted_tokens(units, order, place, build);
        unfilled_vector<token_group> groups(total);   // by rank; as many as tokens at most
        unfilled_vector<std::uint32_t> rank_at(total);
        groups.resize(rank(tokens, groups, rank_at, build));
        const unfilled_vector<std::uint32_t> row_at = keep_prefixes(rank_at, place, build);
        gather_postings(tokens, groups, row_at, build);
    }

    /// How many of a unit's `size` tokens its prefix holds for threshold t.
    [[nodiscard]] static constexpr std::size_t prefix_length(std::size_t size, double t) noexcept {
        if (size == 0) return 0;
        const double overlap = t * static_cast<double>(size) / (2.0 - t);
        std::size_t need = static_cast<std::size_t>(overlap);
        if (static_cast<double>(need) < overlap) ++need;   // ⌈overlap⌉
        if (need == 0) return size;
        return need > size ? 0 : size - need + 1;
    }

    /// Calls f(j) once for every row j > r that may score t or more with row r — its prefix
    /// shares a token with r's, and the weights' ceiling reaches t. `seen` (one per thread, as
    /// many entries as rows, zeroes at first) marks what this row already offered.
    template <class F>
    constexpr void candidates(std::size_t r, std::vector<std::uint32_t>& seen, F&& f) const {
        // the partners the ceiling admits
        const std::size_t end = reach(m_weights[r], r + 1, m_weights.size(), m_threshold, [&](std::size_t j) { return m_weights[j]; });
        const auto mark = static_cast<std::uint32_t>(r + 1);
        for (std::uint32_t i = m_prefix_first[r]; i < m_prefix_first[r + 1]; ++i) {
            const std::uint32_t t_rank = m_prefix[i];
            const auto first = m_post.begin() + m_post_first[t_rank], last = m_post.begin() + m_post_first[t_rank + 1];
            for (auto it = std::upper_bound(first, last, static_cast<std::uint32_t>(r)); it != last && *it < end; ++it) {
                if (seen[*it] == mark) continue;
                seen[*it] = mark;
                f(static_cast<std::size_t>(*it));
            }
        }
    }

    /// Adds to counts[j] how many tokens row r shares with each row j in (r, end): its shared
    /// count, Σ min(count) of the two multisets, when the index holds every token (t = 0).
    /// Cost: the posting lists of r's tokens past r — over all rows, the sum of every pair's
    /// shared count, where merging every pair costs every pair's length.
    constexpr void count_shared(std::size_t r, std::vector<std::uint32_t>& counts, std::size_t end) const { count_shared(r, counts, r + 1, end); }
    /// The same for the rows j in [from, end) only (from > r): a piece of row r's partners, so a
    /// worker touches only the counts it writes out.
    constexpr void count_shared(std::size_t r, std::vector<std::uint32_t>& counts, std::size_t from, std::size_t end) const {
        count_shared_into(r, counts.data() + from, from, end);
    }
    /// The same into a window of rows: out[j - from] for each row j in [from, end), so a worker counts
    /// a tile of rows into a buffer of the tile's size.
    constexpr void count_shared_into(std::size_t r, std::uint32_t* out, std::size_t from, std::size_t end) const {
        for (std::uint32_t i = m_prefix_first[r]; i < m_prefix_first[r + 1]; ++i) {
            const std::uint32_t t_rank = m_prefix[i];
            const auto first = m_post.begin() + m_post_first[t_rank], last = m_post.begin() + m_post_first[t_rank + 1];
            for (auto it = std::lower_bound(first, last, static_cast<std::uint32_t>(from)); it != last && *it < end; ++it) ++out[*it - from];
        }
    }

    /// What candidates(r) walks: the posting lists of r's prefix tokens, every row in them — the
    /// work of a filtered row, which its candidate count does not tell.
    [[nodiscard]] constexpr std::size_t offers(std::size_t r) const noexcept {
        std::size_t n = 0;
        for (std::uint32_t i = m_prefix_first[r]; i < m_prefix_first[r + 1]; ++i) n += m_post_first[m_prefix[i] + 1] - m_post_first[m_prefix[i]];
        return n;
    }

    [[nodiscard]] constexpr std::size_t rows() const noexcept { return m_weights.size(); }
    [[nodiscard]] constexpr double threshold() const noexcept { return m_threshold; }
    [[nodiscard]] constexpr std::uint64_t weight(std::size_t row) const noexcept { return m_weights[row]; }
    /// Every row's weight, in sweep order.
    [[nodiscard]] constexpr const std::vector<std::uint64_t>& weights() const noexcept { return m_weights; }

    /// The same index: the same threshold, weights, prefixes and postings.
    [[nodiscard]] constexpr bool operator==(const token_index&) const = default;

private:
    /// A token's entries among the sorted tokens: as many as rows hold the token, from `begin`.
    class token_group {
    public:
        token_group() = default;
        constexpr token_group(std::uint32_t rows, std::uint32_t begin) noexcept : m_rows(rows), m_begin(begin) {}

        [[nodiscard]] constexpr std::uint32_t rows() const noexcept { return m_rows; }
        [[nodiscard]] constexpr std::uint32_t begin() const noexcept { return m_begin; }
        [[nodiscard]] constexpr std::uint32_t end() const noexcept { return m_begin + m_rows; }

    private:
        std::uint32_t m_rows;
        std::uint32_t m_begin;
    };

    /// What a place holds when its token is not in its row's prefix.
    static constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
    /// How far below t the filter's bounds are computed: a pair whose exact ratio rounds up to t is
    /// admitted by the sweep's `value >= t`, so the filter must not cut it.
    static constexpr double rounding_margin = 1e-9;

    [[nodiscard]] static constexpr std::size_t tasks_for(std::size_t items, std::size_t per) noexcept { return pieces(items, per); }
    /// Task k's items, `per` a task, of `items`: [first, last).
    [[nodiscard]] static constexpr std::pair<std::size_t, std::size_t> share(std::size_t k, std::size_t per, std::size_t items) noexcept {
        return {std::min(items, k * per), std::min(items, (k + 1) * per)};
    }

    /// Where each row's tokens go — row r's at places [place[r], place[r + 1]), returned — and each
    /// row's weight and prefix share (m_weights, m_prefix_first), from the rows' sizes.
    template <class Build>
    constexpr std::vector<std::uint32_t> measure(const std::vector<unit>& units, const std::vector<std::uint32_t>& order, double loose, const Build& build) {
        const std::size_t n = order.size(), per = build.grain().rows();
        std::vector<std::uint32_t> place(n + 1, 0);
        m_weights.resize(n);
        m_prefix_first.assign(n + 1, 0);
        build.for_each(tasks_for(n, per), [&](std::size_t k) {
            for (auto [r, past] = share(k, per, n); r < past; ++r) {
                const unit& u = units[order[r]];
                place[r + 1] = static_cast<std::uint32_t>(u.fp().subtrees().size());
                m_prefix_first[r + 1] = static_cast<std::uint32_t>(prefix_length(place[r + 1], loose));
                m_weights[r] = u.weight();
            }
        });
        std::uint64_t total = 0;   // the places, 32-bit: checked as they are summed
        for (std::size_t r = 0; r < n; ++r) {
            total += place[r + 1];
            place[r + 1] = static_cast<std::uint32_t>(total);
            m_prefix_first[r + 1] += m_prefix_first[r];
        }
        if (total > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("similarity: an index holds at most 2^32 - 1 subtrees, every unit's together");
        return place;
    }

    /// Every row's tokens, sorted. Each task counts its rows' tokens per bucket — the hash's top
    /// bits, about grain.bucket() tokens a bucket, at most 1,024 buckets so a task's open buckets
    /// stay in its L2 — then writes each token straight into its bucket; each bucket is then sorted
    /// by its own task (sort_bucket). The buckets are in hash order, so the result is token_entry's
    /// order. No list of tokens is made only to be scattered: on LLVM, writing one and scattering
    /// it took 12 ms of a 23 ms sort (2026-10-28).
    template <class Build>
    [[nodiscard]] static constexpr unfilled_vector<token_entry> sorted_tokens(const std::vector<unit>& units, const std::vector<std::uint32_t>& order,
                                                                              const std::vector<std::uint32_t>& place, const Build& build) {
        const std::size_t n = order.size(), total = place[n], per = build.grain().rows(), tasks = tasks_for(n, per);
        const unsigned bits = static_cast<unsigned>(std::min<std::size_t>(10, std::bit_width(total / build.grain().bucket())));
        const std::size_t buckets = std::size_t{1} << bits;
        auto bucket = [bits](subtree h) { return bits == 0 ? std::size_t{0} : static_cast<std::size_t>(h >> (64 - bits)); };
        // each task's tokens per bucket, bucket-major, turned into where its share of each bucket
        // starts. Task-major, no two tasks would write one cache line, but the scan below strides:
        // LLVM's index took 40.5-41.3 ms so, 39.6-40.0 as it is (best of 30, 2026-10-29).
        std::vector<std::uint32_t> at(buckets * tasks);
        build.for_each(tasks, [&](std::size_t k) {
            std::vector<std::uint32_t> mine(buckets, 0);
            for (auto [r, past] = share(k, per, n); r < past; ++r) {
                for (subtree h : units[order[r]].fp().subtrees()) ++mine[bucket(h)];
            }
            for (std::size_t b = 0; b < buckets; ++b) at[b * tasks + k] = mine[b];
        });
        std::vector<std::uint32_t> first(buckets + 1, 0);   // bucket b: tokens [first[b], first[b + 1])
        std::uint32_t sum = 0;
        for (std::size_t b = 0; b < buckets; ++b) {
            first[b] = sum;
            for (std::size_t k = 0; k < tasks; ++k) sum += std::exchange(at[b * tasks + k], sum);
        }
        first[buckets] = sum;
        unfilled_vector<token_entry> tokens(total);
        build.for_each(tasks, [&](std::size_t k) {
            std::vector<std::uint32_t> mine(buckets);
            for (std::size_t b = 0; b < buckets; ++b) mine[b] = at[b * tasks + k];
            for (auto [r, past] = share(k, per, n); r < past; ++r) {
                const std::vector<subtree>& s = units[order[r]].fp().subtrees();
                for (std::size_t i = 0; i < s.size(); ++i) tokens[mine[bucket(s[i])]++] = token_entry(s[i], place[r] + static_cast<std::uint32_t>(i));
            }
        });
        build.for_each(buckets, [&](std::size_t b) { sort_bucket(tokens.data() + first[b], tokens.data() + first[b + 1], bits); });
        return tokens;
    }

    /// Sorts tokens whose hashes agree in their top `skip` bits: counted by their next bits into
    /// about two a sub-bucket, then each sub-bucket by insertion — by std::sort where one holds
    /// many, one subtree in many rows. On LLVM's buckets 4 ms where std::sort took 11 (2026-10-28).
    static constexpr void sort_bucket(token_entry* first, token_entry* last, unsigned skip) {
        const std::size_t m = static_cast<std::size_t>(last - first);
        if (m <= 32) {
            insertion_sort(first, last);
            return;
        }
        const auto bits = static_cast<unsigned>(std::min({static_cast<std::size_t>(std::bit_width(m / 2)), std::size_t{16}, std::size_t{64} - skip}));
        auto sub = [skip, bits](const token_entry& e) { return static_cast<std::size_t>((e.hash() << skip) >> (64 - bits)); };
        std::vector<std::uint32_t> begin((std::size_t{1} << bits) + 1, 0);
        for (const token_entry* e = first; e != last; ++e) ++begin[sub(*e) + 1];
        for (std::size_t s = 1; s < begin.size(); ++s) begin[s] += begin[s - 1];
        const std::vector<token_entry> in(first, last);
        std::vector<std::uint32_t> at(begin.begin(), begin.end() - 1);
        for (const token_entry& e : in) first[at[sub(e)]++] = e;
        for (std::size_t s = 0; s + 1 < begin.size(); ++s) {
            token_entry* a = first + begin[s];
            token_entry* z = first + begin[s + 1];
            if (z - a <= 16) insertion_sort(a, z);
            else std::sort(a, z);
        }
    }
    static constexpr void insertion_sort(token_entry* first, token_entry* last) {
        for (token_entry* i = first + (first != last); i < last; ++i) {
            const token_entry x = *i;
            token_entry* j = i;
            for (; j > first && x < j[-1]; --j) *j = j[-1];
            *j = x;
        }
    }

    /// Ranks the sorted tokens' groups by how many rows hold them, rarest first, ties in token
    /// order: a stable counting sort in two passes over tasks of whole groups, the first counting
    /// each task's groups per count, the second writing each group at its rank and that rank at its
    /// tokens' places. Counts of grain.counted() or more are rare — at most total / counted groups —
    /// and sorted apart, after the rest. Returns the number of groups.
    template <class Build>
    static constexpr std::size_t rank(const unfilled_vector<token_entry>& tokens, unfilled_vector<token_group>& groups, unfilled_vector<std::uint32_t>& rank_at,
                                      const Build& build) {
        const std::size_t total = tokens.size(), per = build.grain().tokens(), tasks = tasks_for(total, per);
        const auto counted = static_cast<std::uint32_t>(build.grain().counted());   // with_counted holds it to 32 bits
        std::vector<std::size_t> from(tasks + 1, total);   // task k: groups from the first starting at or after k·per
        for (std::size_t k = 0; k < tasks; ++k) {
            std::size_t b = k * per;
            while (b > 0 && b < total && tokens[b].same_token(tokens[b - 1])) ++b;
            from[k] = b;
        }
        auto each_group = [&](std::size_t k, auto&& f) {
            for (std::size_t i = from[k]; i < from[k + 1];) {
                std::size_t j = i + 1;
                while (j < from[k + 1] && tokens[j].same_token(tokens[i])) ++j;
                f(token_group(static_cast<std::uint32_t>(j - i), static_cast<std::uint32_t>(i)));
                i = j;
            }
        };
        // task k's groups of count c, then where they start: at[k·counted + c] — a row a task, so no
        // two tasks count on one cache line (count-major, they did: rank took 17 ms, not 7)
        std::vector<std::uint32_t> at(std::size_t{counted} * tasks, 0);
        std::vector<std::vector<std::uint32_t>> rare(tasks);   // each task's counts of `counted` or more
        build.for_each(tasks, [&](std::size_t k) {
            each_group(k, [&](token_group g) {
                if (g.rows() < counted) ++at[k * counted + g.rows()];
                else rare[k].push_back(g.rows());
            });
        });
        std::uint32_t common = 0;   // the groups of counts below `counted`: ranks [0, common)
        for (std::size_t c = 0; c < counted; ++c) {
            for (std::size_t k = 0; k < tasks; ++k) common += std::exchange(at[k * counted + c], common);
        }
        std::vector<std::size_t> rare_from(tasks + 1, 0);   // task k's rare groups: [rare_from[k], rare_from[k + 1]) in token order
        std::vector<std::uint32_t> rare_counts;
        for (std::size_t k = 0; k < tasks; ++k) {
            rare_from[k + 1] = rare_from[k] + rare[k].size();
            rare_counts.insert(rare_counts.end(), rare[k].begin(), rare[k].end());
        }
        std::vector<std::uint32_t> by_count(rare_counts.size()), rare_rank(rare_counts.size());
        std::iota(by_count.begin(), by_count.end(), 0u);
        std::sort(by_count.begin(), by_count.end(), [&](std::uint32_t a, std::uint32_t b) { return rare_counts[a] != rare_counts[b] ? rare_counts[a] < rare_counts[b] : a < b; });
        for (std::uint32_t j = 0; j < by_count.size(); ++j) rare_rank[by_count[j]] = common + j;
        build.for_each(tasks, [&](std::size_t k) {
            std::size_t next_rare = rare_from[k];
            each_group(k, [&](token_group g) {
                const std::uint32_t r = g.rows() < counted ? at[k * counted + g.rows()]++ : rare_rank[next_rare++];
                groups[r] = g;
                for (std::uint32_t e = g.begin(); e < g.end(); ++e) rank_at[tokens[e].place()] = r;
            });
        });
        return common + rare_counts.size();
    }

    /// Each row's prefix — its rarest prefix-length tokens, as ranks ascending — into m_prefix;
    /// returns at each place its row where its token is in the row's prefix, else `none`: the rows
    /// the postings gather.
    template <class Build>
    constexpr unfilled_vector<std::uint32_t> keep_prefixes(const unfilled_vector<std::uint32_t>& rank_at, const std::vector<std::uint32_t>& place, const Build& build) {
        const std::size_t n = place.size() - 1, per = build.grain().rows();
        m_prefix.resize(m_prefix_first[n]);
        unfilled_vector<std::uint32_t> row_at(place[n]);
        build.for_each(tasks_for(n, per), [&](std::size_t k) {
            std::vector<std::uint32_t> ranks;
            for (auto [r, past] = share(k, per, n); r < past; ++r) {
                const std::uint32_t len = m_prefix_first[r + 1] - m_prefix_first[r];
                ranks.assign(rank_at.begin() + place[r], rank_at.begin() + place[r + 1]);
                if (len > 0 && len < ranks.size()) std::nth_element(ranks.begin(), ranks.begin() + len, ranks.end());
                std::sort(ranks.begin(), ranks.begin() + len);
                std::copy(ranks.begin(), ranks.begin() + len, m_prefix.begin() + m_prefix_first[r]);
                const std::uint32_t last = len == 0 ? 0 : ranks[len - 1];   // the prefix's most common token
                for (std::uint32_t p = place[r]; p < place[r + 1]; ++p) row_at[p] = len > 0 && rank_at[p] <= last ? static_cast<std::uint32_t>(r) : none;
            }
        });
        return row_at;
    }

    /// Each rank's rows — those whose prefix holds its token — ascending, as a group's tokens are in
    /// place order: two passes over tasks of ranks, counting then gathering.
    template <class Build>
    constexpr void gather_postings(const unfilled_vector<token_entry>& tokens, const unfilled_vector<token_group>& groups, const unfilled_vector<std::uint32_t>& row_at,
                                   const Build& build) {
        const std::size_t ranks = groups.size(), per = build.grain().ranks(), tasks = tasks_for(ranks, per);
        m_post_first.resize(ranks + 1);
        m_post_first[0] = 0;
        std::vector<std::size_t> task_first(tasks + 1, 0);
        build.for_each(tasks, [&](std::size_t k) {
            std::size_t sum = 0;
            for (auto [g, past] = share(k, per, ranks); g < past; ++g) {
                std::uint32_t rows = 0;
                for (std::uint32_t e = groups[g].begin(); e < groups[g].end(); ++e) rows += row_at[tokens[e].place()] != none;
                m_post_first[g + 1] = rows;
                sum += rows;
            }
            task_first[k + 1] = sum;
        });
        std::inclusive_scan(task_first.begin(), task_first.end(), task_first.begin());
        m_post.resize(task_first[tasks]);
        build.for_each(tasks, [&](std::size_t k) {
            std::size_t at = task_first[k];
            for (auto [g, past] = share(k, per, ranks); g < past; ++g) {
                for (std::uint32_t e = groups[g].begin(); e < groups[g].end(); ++e) {
                    if (const std::uint32_t r = row_at[tokens[e].place()]; r != none) m_post[at++] = r;
                }
                m_post_first[g + 1] = static_cast<std::uint32_t>(at);
            }
        });
    }

    double m_threshold;
    std::vector<std::uint64_t> m_weights;        // each row's weight
    std::vector<std::uint32_t> m_prefix_first;   // row r's prefix: m_prefix[first[r], first[r + 1])
    unfilled_vector<std::uint32_t> m_prefix;       // token ranks, rarest first
    unfilled_vector<std::uint32_t> m_post_first;   // token rank g's rows: m_post[first[g], first[g + 1])
    unfilled_vector<std::uint32_t> m_post;         // rows, ascending within a token
};
static_assert([] {   // filtered_row's use: two units sharing every subtree meet; a third shares none
    std::vector<unit> us;
    us.emplace_back(unit_kind::function, "f", 1, fingerprint(0, 3, {1, 2, 3}));
    us.emplace_back(unit_kind::function, "g", 2, fingerprint(0, 3, {1, 2, 3}));
    us.emplace_back(unit_kind::function, "h", 3, fingerprint(0, 3, {4, 5, 6}));
    const token_index ix(us, {0, 1, 2}, 0.8);
    std::vector<std::uint32_t> seen(3, 0), found;
    ix.candidates(0, seen, [&](std::size_t j) { found.push_back(static_cast<std::uint32_t>(j)); });
    return found == std::vector<std::uint32_t>{1} && ix.offers(0) >= found.size() && ix.offers(2) == 2;   // h's prefix: two of its tokens, h's alone
}());

/// The pairs of a sweep over `rows` rows, numbered in sweep order: row r's partners r + 1 … rows - 1
/// hold places start(r) … start(r + 1) - 1. A sweep at threshold 0 keeps every pair, so each one's
/// place in the result is known before any is scored: workers can write places [p, q) — pieces of
/// rows, cut anywhere — straight to where they belong, in any order (placed_sweep_law).
///
///     sweep_positions s(4);          // (0,1) (0,2) (0,3) (1,2) (1,3) (2,3)
///     s.start(1) -> 3     s.row_of(4) -> 1     s.pairs() -> 6
///     s.for_each_piece(2, 5, f)      // f(0, 3, 4, 2): row 0's partners [3, 4) at place 2
///                                    // f(1, 2, 4, 3): row 1's partners [2, 4) at place 3
class sweep_positions {
public:
    constexpr explicit sweep_positions(std::size_t rows) noexcept : m_rows(rows) {}

    [[nodiscard]] constexpr std::size_t rows() const noexcept { return m_rows; }
    [[nodiscard]] constexpr std::size_t pairs() const noexcept { return start(m_rows); }
    /// The place of row r's first pair: the sum of rows - q - 1 over q < r (r ≤ rows).
    [[nodiscard]] constexpr std::size_t start(std::size_t r) const noexcept { return r * (2 * m_rows - r - 1) / 2; }
    /// The row whose pairs hold place p (p < pairs()).
    [[nodiscard]] constexpr std::size_t row_of(std::size_t p) const noexcept {
        std::size_t lo = 0, hi = m_rows;   // the last row starting at or before p
        while (hi - lo > 1) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (start(mid) <= p) lo = mid;
            else hi = mid;
        }
        return lo;
    }
    /// f(row, from, end, at) for each piece of a row that places [p, q) cover, in order: the row's
    /// partners [from, end), the first of them at place `at`. An f that returns a bool stops the
    /// walk by returning false: place_tiles, once a tile is cut, needs no more of its pieces.
    template <class F>
    constexpr void for_each_piece(std::size_t p, std::size_t q, F&& f) const {
        for (std::size_t r = p < q ? row_of(p) : 0; p < q; ++r) {
            const std::size_t from = r + 1 + (p - start(r));
            const std::size_t end = std::min(m_rows, from + (q - p));
            if constexpr (std::is_same_v<decltype(f(r, from, end, p)), bool>) {
                if (!f(r, from, end, p)) return;
            } else {
                f(r, from, end, p);
            }
            p += end - from;
        }
    }

private:
    std::size_t m_rows;
};
static_assert([] {   // the example above
    const sweep_positions s(4);
    std::vector<std::size_t> got;
    s.for_each_piece(2, 5, [&](std::size_t r, std::size_t from, std::size_t end, std::size_t at) { got.insert(got.end(), {r, from, end, at}); });
    std::size_t walked = 0;   // a walk stopped after its first piece
    s.for_each_piece(2, 5, [&](std::size_t, std::size_t, std::size_t, std::size_t) { return ++walked < 1; });
    return s.start(1) == 3 && s.row_of(4) == 1 && s.pairs() == 6 && got == std::vector<std::size_t>{0, 3, 4, 2, 1, 2, 4, 3} && walked == 1;
}());

/// What a class sweep counts with instead of merging every method pair: an index of every token of
/// the classes' own subtrees (rows: the classes of a sweep order) and one of their methods' (rows:
/// the methods, class by class in that order, each class's in its own order). A class row's shared
/// counts — its header's with each later class's, each of its methods' with each of theirs — come
/// from posting lists (counted_row's way), and only each pair's method assignment is computed
/// (class_counted_law). On CPython's stdlib (2,625 classes) the method cells of every class pair
/// were counted in 94 ms where merging them took 5.2 s (2026-10-27).
class class_index {
public:
    template <class Build = sequential_build>
    constexpr class_index(const std::vector<unit>& units, const std::vector<std::uint32_t>& order, const Build& build = Build{})
        : m_headers(units, order, 0.0, build), m_first(method_starts(units, order)), m_methods(units, method_rows(units, order), 0.0, build) {}

    [[nodiscard]] constexpr const token_index& headers() const noexcept { return m_headers; }
    [[nodiscard]] constexpr const token_index& methods() const noexcept { return m_methods; }
    /// Class row r's methods are method rows [first(r), first(r + 1)).
    [[nodiscard]] constexpr std::uint32_t first(std::size_t r) const noexcept { return m_first[r]; }

private:
    [[nodiscard]] static constexpr std::vector<std::uint32_t> method_rows(const std::vector<unit>& units, const std::vector<std::uint32_t>& order) {
        std::vector<std::uint32_t> rows;
        for (std::uint32_t c : order) rows.insert(rows.end(), units[c].methods().begin(), units[c].methods().end());
        return rows;
    }
    [[nodiscard]] static constexpr std::vector<std::uint32_t> method_starts(const std::vector<unit>& units, const std::vector<std::uint32_t>& order) {
        std::vector<std::uint32_t> first(order.size() + 1, 0);
        for (std::size_t r = 0; r < order.size(); ++r) first[r + 1] = first[r] + static_cast<std::uint32_t>(units[order[r]].methods().size());
        return first;
    }

    token_index m_headers;
    std::vector<std::uint32_t> m_first;
    token_index m_methods;
};
static_assert([] {   // two classes of one method each: a header row each, and their methods' rows class by class
    auto body = [] {
        tree_builder b;
        b.open("Return");
        b.leaf("Name", "a", role::use);
        b.close();
        return std::move(b).finish();
    };
    parsed_source<detail::example_language> src;
    src.add(parsed_unit(unit_kind::function, "K.m", 2, body()));
    src.add(parsed_unit(unit_kind::class_, "K", 1, body(), {0}));
    src.add(parsed_unit(unit_kind::function, "L.m", 4, body()));
    src.add(parsed_unit(unit_kind::class_, "L", 3, body(), {2}));
    const prepared_source<detail::example_language> ready(std::move(src));
    const class_index ix(ready.units(), {1, 3});
    return ix.headers().rows() == 2 && ix.methods().rows() == 2 && ix.first(1) == 1 && ix.first(2) == 2;
}());

/// Counts lent for one use from a buffer of zeroes, and zeroed again when the use ends — by its end
/// or by an exception — so the buffer is zeroes for the next: what a worker's buffers keep, row
/// after row, however a row ends.
///
///     { const zeroed_counts c(buffer.data() + from, n);  ++c[0]; }   // buffer[from] is 0 again
class zeroed_counts {
public:
    constexpr zeroed_counts(std::uint32_t* first, std::size_t size) noexcept : m_first(first), m_size(size) {}
    zeroed_counts(const zeroed_counts&) = delete;
    zeroed_counts& operator=(const zeroed_counts&) = delete;
    constexpr ~zeroed_counts() { std::fill(m_first, m_first + m_size, 0u); }

    [[nodiscard]] constexpr std::uint32_t* data() const noexcept { return m_first; }
    [[nodiscard]] constexpr std::uint32_t& operator[](std::size_t i) const noexcept { return m_first[i]; }

private:
    std::uint32_t* m_first;
    std::size_t m_size;
};
static_assert([] {   // the example above
    std::vector<std::uint32_t> buffer(4, 0);
    {
        const zeroed_counts c(buffer.data() + 1, 2);
        ++c[0];
        if (buffer[1] != 1) return false;
    }
    return buffer == std::vector<std::uint32_t>(4, 0);
}());

/// A worker's buffers for sweep_rows::counted_class_row, reused row after row: the header counts (one
/// per class row), a tile's method counts (a run of the tile's width per method of the row's
/// class) and the assignment's. Each count is lent as zeroed_counts, so the buffers are zeroes
/// between uses. `tile_cells`: the counts a tile may hold, sized to the L2 where the adapter knows it.
class class_row_scratch {
public:
    /// 256 KB of counts, where the adapter does not size the tile: half a 512 KB L2.
    static constexpr std::size_t default_tile_cells = 64 * 1024;

    constexpr explicit class_row_scratch(std::size_t classes, std::size_t tile_cells = default_tile_cells)
        : m_headers(classes, 0), m_tile_cells(tile_cells) {}

    [[nodiscard]] constexpr std::size_t tile_cells() const noexcept { return m_tile_cells; }
    /// The header counts of class rows [from, end): row j's at [j - from].
    [[nodiscard]] constexpr zeroed_counts headers(std::size_t from, std::size_t end) noexcept { return {m_headers.data() + from, end - from}; }
    /// `cells` method counts.
    [[nodiscard]] constexpr zeroed_counts methods(std::size_t cells) {
        if (m_methods.size() < cells) m_methods.resize(cells, 0);
        return {m_methods.data(), cells};
    }
    /// The assignment's buffers, kept from pair to pair.
    [[nodiscard]] constexpr assignment_solver& solver() noexcept { return m_solver; }
    /// Every count zero: what every use leaves.
    [[nodiscard]] constexpr bool zeroes() const noexcept {
        return std::all_of(m_headers.begin(), m_headers.end(), [](std::uint32_t k) { return k == 0; }) &&
               std::all_of(m_methods.begin(), m_methods.end(), [](std::uint32_t k) { return k == 0; });
    }

private:
    std::vector<std::uint32_t> m_headers;
    std::vector<std::uint32_t> m_methods;
    assignment_solver m_solver;
    std::size_t m_tile_cells;
};
static_assert([] {
    class_row_scratch s(4, 8);
    ++s.headers(1, 3)[0];
    ++s.methods(6)[5];
    return s.tile_cells() == 8 && s.zeroes();
}());

/// Every unit of one language, and the comparisons between them.
///
///     corpus<python> c;
///     c.add(python::parse(text, "m.py"), "m.py");
///     c.pairs(unit_kind::function)        // every function against every other
///
/// Lifetime: a value; owns copies of everything added. Not synchronised.
template <language L>
class corpus {
public:
    using language_type = L;
    /// The subtrees a corpus holds at most, every unit's fingerprint together: what keeps every
    /// score's counts, and every index's places, within 32 bits. LLVM's functions hold 3.1M.
    static constexpr std::uint64_t max_subtrees = std::numeric_limits<std::uint32_t>::max();

    /// Adds every unit of `src`, recording `origin` (a file name, "<string>")
    /// once. Returns the index of the first unit added.
    /// Cost: one fingerprint per unit, O(n log n) in its nodes.
    constexpr std::uint32_t add(parsed_source<L> src, std::string_view origin) { return add(prepared_source<L>(std::move(src)), origin); }

    /// Adds units already fingerprinted (on any thread): their ids follow the units already here,
    /// as add() would give them. Past max_subtrees, std::length_error, and nothing is added.
    /// Cost: the moves.
    constexpr std::uint32_t add(prepared_source<L> src, std::string_view origin) {
        std::uint64_t subtrees = 0;
        for (const unit& u : src.units()) subtrees += u.fp().weight();
        if (subtrees > max_subtrees - m_subtrees) throw std::length_error("similarity: an index holds at most 2^32 - 1 subtrees");
        m_subtrees += subtrees;
        const auto first = static_cast<std::uint32_t>(m_units.size());
        const auto from = static_cast<std::uint32_t>(m_origins.size());
        m_origins.emplace_back(origin);
        for (unit& u : std::move(src).release()) {   // no reserve: an exact one per source would re-move every unit each time
            u.place(from, first);
            m_units.push_back(std::move(u));
        }
        return first;
    }

    /// Room for `units` more units from `sources` more sources: adding many sources then moves
    /// each unit once (add_files knows them all before it adds the first).
    constexpr void reserve(std::size_t units, std::size_t sources) {
        m_units.reserve(m_units.size() + units);
        m_origins.reserve(m_origins.size() + sources);
    }

    [[nodiscard]] constexpr const std::vector<unit>& units() const noexcept { return m_units; }
    [[nodiscard]] constexpr const std::vector<std::string>& origins() const noexcept { return m_origins; }

    /// The bytes the corpus holds: itself, every vector at its capacity, and
    /// every string's buffer at its capacity (a short string's in-object
    /// buffer is inside its owner's sizeof, so only longer ones add).
    /// Cost: one pass over the units.
    [[nodiscard]] constexpr std::size_t bytes() const noexcept {
        std::size_t b = sizeof(*this) + m_units.capacity() * sizeof(unit) + m_origins.capacity() * sizeof(std::string);
        for (const std::string& o : m_origins) b += string_heap_bytes(o);
        for (const unit& u : m_units) b += u.heap_bytes();
        return b;
    }

    /// The score of units a and b, which must be of one kind (else
    /// std::invalid_argument). Functions: dice(). Classes: header overlap plus
    /// the best method assignment, over both classes' whole weight.
    /// Cost: functions O(|a| + |b|); classes one dice per method pair plus
    /// the assignment.
    [[nodiscard]] constexpr score compare(std::uint32_t a, std::uint32_t b) const {
        const unit& x = m_units.at(a);
        const unit& y = m_units.at(b);
        if (x.kind() != y.kind()) throw std::invalid_argument("similarity: a function is compared only with a function, a class only with a class");
        if (x.kind() == unit_kind::function) return dice(x.fp(), y.fp());
        std::vector<std::uint64_t> w(x.methods().size() * y.methods().size());
        for (std::size_t i = 0; i < x.methods().size(); ++i) {
            for (std::size_t j = 0; j < y.methods().size(); ++j) {
                w[i * y.methods().size() + j] = shared_weight(m_units[x.methods()[i]].fp(), m_units[y.methods()[j]].fp());
            }
        }
        const std::uint64_t matched = best_assignment(w, x.methods().size(), y.methods().size());
        return score{shared_weight(x.fp(), y.fp()) + matched, x.weight() + y.weight()};
    }

    /// Every pair (a < b) of units of `kind` with at least `min_size` nodes
    /// each whose score is at least `threshold`, in before() order — and with
    /// a `limit`, only the first `limit` of that list. With threshold 0,
    /// min_size 0 and no limit that is all n(n-1)/2 pairs.
    ///
    /// The sweep is sweep_order() + sweep_rows::sweep_row() for every row
    /// into a collector + finish(): the steps are public so a caller can
    /// spread the rows over threads, one collector each (adapter.h does), or
    /// stream them unranked, and still get exactly this result.
    /// Cost: O(n²) comparisons in the worst case; see sweep_row() for the cut.
    [[nodiscard]] constexpr matches pairs(unit_kind kind, double threshold = 0.0,
                                          std::uint32_t min_size = 0, std::size_t limit = 0) const {
        const std::vector<std::uint32_t> order = sweep_order(kind, min_size);
        const sweep_rows<L> rows(*this);
        collector c{threshold, limit};
        for (std::size_t row = 0; row < order.size(); ++row) rows.sweep_row(order, row, c);
        return finish(std::move(c));
    }

    /// A collector's pairs as pairs() returns them: cut to the limit, sorted.
    [[nodiscard]] static constexpr matches finish(collector c) { return std::move(c).ranked(); }

    /// The units a sweep visits: those of `kind` with at least `min_size`
    /// nodes (a class counts its header's and its methods'), lightest first,
    /// ties by index.
    [[nodiscard]] constexpr std::vector<std::uint32_t> sweep_order(unit_kind kind, std::uint32_t min_size) const {
        std::vector<std::uint32_t> order = sweep_order(kind);
        std::erase_if(order, [&](std::uint32_t i) { return nodes(i) < min_size; });
        return order;
    }
    /// Every unit of `kind` in sweep order: weights ascending, ties by id. Leaving out the small
    /// ones keeps the order of the rest, so an order of every size serves every min_size.
    [[nodiscard]] constexpr std::vector<std::uint32_t> sweep_order(unit_kind kind) const {
        std::vector<std::uint32_t> order;
        for (std::uint32_t i = 0; i < m_units.size(); ++i) {
            if (m_units[i].kind() == kind) order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [&](std::uint32_t l, std::uint32_t r) {
            return m_units[l].weight() != m_units[r].weight() ? m_units[l].weight() < m_units[r].weight() : l < r;
        });
        return order;
    }

    /// The node count of unit i: its tree's, plus its methods' for a class.
    [[nodiscard]] constexpr std::size_t nodes(std::uint32_t i) const {
        std::size_t n = m_units.at(i).fp().nodes();
        for (std::uint32_t m : m_units[i].methods()) n += m_units[m].fp().nodes();
        return n;
    }

private:
    std::vector<unit> m_units;
    std::vector<std::string> m_origins;
    std::uint64_t m_subtrees = 0;   // every unit's fingerprint's, together
};
static_assert([] {   // room reserved, nothing added
    corpus<detail::example_language> c;
    c.reserve(3, 1);
    return c.units().capacity() >= 3 && c.units().empty();
}());

/// The rows of a sweep over one corpus, each counted one way: sweep_row() merges every pair, the
/// definition the others are proven against; filtered_row() asks a prefix filter; counted_row(),
/// counted_class_row() and counted_class_partners() take shared counts from token indexes. What
/// corpus::pairs() and the adapter's sweeps and streams run, row after row; a corpus only holds
/// the units. A view: the corpus outlives it, unchanged.
///
///     const sweep_rows rows(c);
///     for (std::size_t r = 0; r < order.size(); ++r) rows.sweep_row(order, r, into);   // pairs()'s
template <language L>
class sweep_rows {
public:
    constexpr explicit sweep_rows(const corpus<L>& c) noexcept : m_corpus(c) {}

    /// sweep_row() through a token_index: the same pairs (prefix_filter_law), with only the
    /// partners the index offers compared. For functions (a class's score is not one set's Dice),
    /// into a collector of the index's threshold — with a limit too, whose rising floor then bars
    /// what could not be kept. `seen`: the index's per-thread marks.
    constexpr void filtered_row(const token_index& index, const std::vector<std::uint32_t>& order, std::size_t row,
                                std::vector<std::uint32_t>& seen, collector& into) const {
        const std::uint32_t a = order[row];
        index.candidates(row, seen, [&](std::size_t j) {
            const std::uint32_t b = order[j];
            const match m(a, b, dice(m_corpus.units()[a].fp(), m_corpus.units()[b].fp()));
            if (m.value() >= into.floor()) into.add(m);
        });
    }
    /// filtered_row() in sweep order: the partners the index offers, sorted, then scored — sweep_row()'s
    /// pairs at the index's threshold, in its order (filtered_order_law), as a stream emits them.
    /// `partners`: a per-thread buffer, any contents.
    constexpr void filtered_row_in_order(const token_index& index, const std::vector<std::uint32_t>& order, std::size_t row,
                                         std::vector<std::uint32_t>& seen, std::vector<std::uint32_t>& partners, collector& into) const {
        partners.clear();
        index.candidates(row, seen, [&](std::size_t j) { partners.push_back(static_cast<std::uint32_t>(j)); });
        std::sort(partners.begin(), partners.end());
        const std::uint32_t a = order[row];
        for (const std::uint32_t j : partners) {
            const std::uint32_t b = order[j];
            const match m(a, b, dice(m_corpus.units()[a].fp(), m_corpus.units()[b].fp()));
            if (m.value() >= into.floor()) into.add(m);
        }
    }

    /// sweep_row() for functions with every shared count taken from `all` (a token_index of every
    /// token, threshold 0) instead of a merge per partner: the same pairs, scores and order
    /// (counted_row_law). `counts`: one per thread, as many entries as rows, zeroes, left zeroes —
    /// also when `into` throws.
    constexpr void counted_row(const token_index& all, const std::vector<std::uint32_t>& order, std::size_t row,
                               std::vector<std::uint32_t>& counts, collector& into) const {
        const std::uint32_t a = order[row];
        const std::uint64_t wa = m_corpus.units()[a].weight();
        const std::size_t end = reach(wa, row + 1, order.size(), into.floor(), weight_in(order));   // the floor only rises: no further
        const zeroed_counts shared(counts.data() + row + 1, end - row - 1);   // partner j's at [j - row - 1]
        all.count_shared_into(row, shared.data(), row + 1, end);
        for (std::size_t j = row + 1; j < end; ++j) {
            const std::uint32_t b = order[j];
            const double bar = into.floor();
            if (bar > 0.0 && ceiling(wa, m_corpus.units()[b].weight()) < bar) break;
            const match m(a, b, score(shared[j - row - 1], wa + m_corpus.units()[b].weight()));
            if (m.value() >= bar) into.add(m);
        }
    }

    /// Class row `row`'s scores with its partners [from, end), every shared count taken from `ix`
    /// instead of a merge per pair: the header's with each partner's, each method's with each of
    /// the partner's methods (partners in tiles whole classes at a time, as many methods as
    /// `scratch`'s tile holds), then each pair's method assignment. found(j, score) for each partner
    /// in order while floor() — 0, or a collector's rising floor — lets it: a partner whose ceiling
    /// falls below it ends the piece, as no heavier one reaches it, and one whose bound cannot reach
    /// it is skipped. The scratch is left zeroes, even by an exception from `found`. A piece of a
    /// row is what a stream's tile writes.
    template <class Floor, class Found>
    constexpr void counted_class_partners(const class_index& ix, const std::vector<std::uint32_t>& order, std::size_t row, std::size_t from,
                                          std::size_t end, class_row_scratch& scratch, const Floor& floor, Found&& found) const {
        const unit& x = m_corpus.units()[order[row]];
        const std::uint64_t wa = x.weight();
        const zeroed_counts head = scratch.headers(from, end);   // partner j's at [j - from]
        ix.headers().count_shared_into(row, head.data(), from, end);
        const std::size_t mx = x.methods().size();
        const std::size_t tile = std::max<std::size_t>(1, scratch.tile_cells() / std::max<std::size_t>(mx, 1));
        bool stopped = false;
        for (std::size_t j = from; j < end && !stopped;) {
            std::size_t k = j + 1;   // the tile: whole classes, as many methods as fit, one class at least
            while (k < end && ix.first(k + 1) - ix.first(j) <= tile) ++k;
            const std::uint32_t lo = ix.first(j), width = ix.first(k) - lo;
            const zeroed_counts counts = scratch.methods(mx * width);
            for (std::size_t i = 0; i < mx; ++i) ix.methods().count_shared_into(ix.first(row) + i, counts.data() + i * width, lo, lo + width);
            for (; j < k; ++j) {
                const unit& y = m_corpus.units()[order[j]];
                const double bar = floor();
                if (bar > 0.0 && ceiling(wa, y.weight()) < bar) {
                    stopped = true;
                    break;
                }
                const std::size_t my = y.methods().size(), at = ix.first(j) - lo;
                auto shared = [&](std::size_t i, std::size_t q) -> std::uint64_t { return counts[i * width + at + q]; };   // method i's with y's q
                // bound() reads the tile twice and only reads: folded into best()'s one pass, which
                // writes its costs before it can stop, Lib/test's classes at 0.5 took 163 ms, not 135
                if (bar > 0.0 && score(head[j - from] + assignment_solver::bound(mx, my, shared), wa + y.weight()).value() < bar) continue;   // cannot reach it
                found(j, score(head[j - from] + scratch.solver().best(mx, my, shared), wa + y.weight()));
            }
        }
    }

    /// sweep_row() for classes, every shared count taken from `ix` (counted_class_partners): the
    /// same pairs, scores and order (class_counted_law).
    constexpr void counted_class_row(const class_index& ix, const std::vector<std::uint32_t>& order, std::size_t row,
                                     class_row_scratch& scratch, collector& into) const {
        const std::uint32_t a = order[row];
        const std::uint64_t wa = m_corpus.units()[a].weight();
        const std::size_t end = reach(wa, row + 1, order.size(), into.floor(), weight_in(order));   // the floor only rises: no further
        counted_class_partners(ix, order, row, row + 1, end, scratch, [&] { return into.floor(); }, [&](std::size_t j, const score& s) {
            const std::uint32_t b = order[j];
            const match m(a, b, s);
            if (m.value() >= into.floor()) into.add(m);
        });
    }

    /// Row `row` of the sweep: unit order[row] against every heavier unit after it, into `into` —
    /// the definition the counted and filtered rows are proven against. The partners come lightest
    /// first, so their ceiling() only falls: the first one below the collector's floor ends the
    /// row, as every later one could not have been kept.
    constexpr void sweep_row(const std::vector<std::uint32_t>& order, std::size_t row, collector& into) const {
        const std::uint32_t a = order[row];
        for (std::size_t j = row + 1; j < order.size(); ++j) {
            const std::uint32_t b = order[j];
            const double bar = into.floor();
            if (bar > 0.0 && ceiling(m_corpus.units()[a].weight(), m_corpus.units()[b].weight()) < bar) break;
            const score s = m_corpus.compare(a, b);
            const match m(a, b, s);
            if (m.value() >= bar) into.add(m);
        }
    }

private:
    // Partner j's weight, for reach(): the unit at order[j].
    [[nodiscard]] constexpr auto weight_in(const std::vector<std::uint32_t>& order) const noexcept {
        return [this, &order](std::size_t j) { return m_corpus.units()[order[j]].weight(); };
    }

    const corpus<L>& m_corpus;
};
static_assert([] {   // the example above: two exact copies, row by row
    parsed_source<detail::example_language> src;
    for (std::string_view name : {"f", "g"}) {
        tree_builder b;
        b.open("Return");
        b.leaf("Name", "a", role::use);
        b.close();
        src.add(parsed_unit(unit_kind::function, std::string(name), 1, std::move(b).finish()));
    }
    corpus<detail::example_language> c;
    c.add(std::move(src), "m.example");
    const std::vector<std::uint32_t> order = c.sweep_order(unit_kind::function, 0);
    const sweep_rows rows(c);
    collector into;
    for (std::size_t r = 0; r < order.size(); ++r) rows.sweep_row(order, r, into);
    return into.kept().size() == 1 && into.kept()[0].score().exact();
}());

static_assert([] {   // the file comment's A and B: one tree once the parameter is renamed
    auto source = [](std::string_view param) {
        parsed_source<detail::example_language> src;
        tree_builder b;
        b.open("FunctionDef");
        b.leaf("arg.arg", param, role::bind);
        b.open("Return");
        b.leaf("Name", param, role::use);
        b.close();
        b.close();
        src.add(parsed_unit(unit_kind::function, std::string(param), 1, std::move(b).finish()));
        return src;
    };
    corpus<detail::example_language> c;
    c.add(source("a"), "a.example");
    c.add(source("b"), "b.example");
    const matches found = c.pairs(unit_kind::function);
    return found.size() == 1 && found[0].score().exact();
}());

// ── errors ────────────────────────────────────────────────────────────────

/// A source a frontend cannot read: the message is "<origin>:<line>: <what>".
/// An invalid_argument, so the Python face raises ValueError.
class parse_error : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] inline parse_error make_parse_error(std::string_view origin, std::uint32_t line, std::string_view what) {
    std::string msg(origin);
    msg += ':';
    msg += std::to_string(line);
    msg += ": ";
    msg += what;
    return parse_error(msg);
}

}  // namespace pygim::similarity
