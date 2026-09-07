// Compile-time proofs for mapping/intern.h, mapping/trie.h and mapping/id_set.h —
// the interner laws (over BOTH engines), the trie laws and the id_set laws.
//
// This TU is part of the pygim.registry extension's SOURCES (ext.registry.toml),
// beside the storage proofs: it is compiled by every build, so a violated law
// cannot produce a binary. It contributes no runtime code.
//
// Every proof is a consteval function returning a bool computed on local
// objects (the GCC 13/14 discipline: nothing string-shaped escapes into the
// assertion expression).

#include "../../src/_pygim_fast/mapping/id_set.h"
#include "../../src/_pygim_fast/mapping/intern.h"
#include "../../src/_pygim_fast/mapping/trie.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

using pygim::mapping::flat_interner;
using pygim::mapping::hashed_interner;
using pygim::mapping::basic_id_set;
using pygim::mapping::id_set;
using pygim::mapping::interner;
using pygim::mapping::trie;

// ── interner laws ──────────────────────────────────────────────────────────
template <interner I>
consteval bool interner_laws() {
    I t;
    const auto a = t.intern("home");
    const auto b = t.intern("var");
    bool ok = a == 0 && b == 1 && t.intern("home") == a && t.size() == 2;       // dense ids in insertion order; intern is idempotent
    ok = ok && t[a] == "home" && t[b] == "var";                                  // id -> bytes round-trips
    ok = ok && t.find("home") == a && t.find("var") == b && t.find("usr") == I::npos && t.size() == 2;   // find never adds
    const auto e = t.intern("");                                                 // the empty string is a key too
    ok = ok && e == 2 && t[e].empty() && t.find("") == e;
    const auto h = t.intern("hom");                                              // a prefix is a distinct key
    ok = ok && h != a && t.find("hom") == h && t.find("home") == a;
    t.reserve(1000, 4096);                                                       // reserve keeps every id
    ok = ok && t.find("home") == a && t.find("var") == b && t.intern("home") == a && t.size() == 4;
    for (int i = 0; i < 200; ++i) {                                              // growth keeps every earlier id
        const char buf[5] = {'k', static_cast<char>('a' + i % 26), static_cast<char>('a' + (i / 26) % 26),
                             static_cast<char>('0' + i % 10), '\0'};
        (void)t.intern(std::string_view(buf, 4));
    }
    ok = ok && t.size() == 204 && t.find("home") == a && t.find("var") == b && t[a] == "home" && t[203] == "krh9";
    ok = ok && t.bytes() > 0;
    return ok;
}
static_assert(interner_laws<flat_interner>());
static_assert(interner_laws<hashed_interner>());
static_assert(!flat_interner::hashed && hashed_interner::hashed);

// ── trie laws ──────────────────────────────────────────────────────────────
consteval bool trie_laws() {
    trie t;
    const auto root = t.root(7);
    const auto a = t.child(root, 1);
    const auto b = t.child(a, 2);
    bool ok = root == 0 && a == 1 && b == 2 && t.child(root, 1) == a && t.size() == 3;   // hash-consed: a shared prefix is one row
    ok = ok && t.is_root(root) && !t.is_root(a) && t.parent(root) == trie::none && t.parent(a) == root && t.parent(b) == a;
    ok = ok && t.key(root) == 7 && t.key(b) == 2;
    ok = ok && t.find_child(root, 1) == a && t.find_child(root, 3) == trie::none && t.find_child(a, 2) == b;
    ok = ok && t.find_root(7) == root && t.find_root(8) == trie::none && t.find_child(root, trie::no_key) == trie::none;
    ok = ok && t.depth(root) == 0 && t.depth(a) == 1 && t.depth(b) == 2 && t.root_of(b) == root && t.root_of(root) == root;
    ok = ok && t.with_chain(b, [&](const std::uint32_t* rows, std::size_t n) {   // leaf first
        return n == 3 && rows[0] == b && rows[1] == a && rows[2] == root;
    });
    std::uint32_t seen[3] = {0, 0, 0};
    std::size_t k = 0;
    auto record = [&](std::uint32_t r) { seen[k++] = r; };
    t.for_chain(b, record);                                                      // root first
    ok = ok && k == 3 && seen[0] == root && seen[1] == a && seen[2] == b;
    std::uint32_t cur = root;                                                    // deeper than the stack buffer
    for (std::uint32_t i = 0; i < 100; ++i) cur = t.child(cur, 100 + i);
    ok = ok && t.depth(cur) == 100 && t.with_chain(cur, [&](const std::uint32_t* rows, std::size_t n) {
        return n == 101 && rows[0] == cur && rows[n - 1] == root;
    });
    t.reserve(4096);                                                             // reserve keeps every row
    ok = ok && t.find_child(root, 1) == a && t.find_child(a, 2) == b && t.child(a, 2) == b && t.rows().size() == t.size();

    trie other;                                                                  // another key space: keys shifted by 10
    const auto oroot = other.root(17);
    const auto oa = other.child(oroot, 11);
    const auto ob = other.child(oa, 12);
    const auto oc = other.child(oa, 13);
    auto shift = [](std::uint32_t key) { return key == trie::no_key ? key : key - 10; };
    trie::row_map<decltype(shift)> look(t, other, nullptr, shift);              // lookup only: absent rows map to none
    ok = ok && look(ob) == b && look(oa) == a && look(oc) == trie::none && look(ob) == b;
    const std::size_t before = t.size();
    trie::row_map<decltype(shift)> add(t, other, &t, shift);                    // find-or-add: absent rows are created under the mapped parent
    const auto c = add(oc);
    ok = ok && c != trie::none && t.size() == before + 1 && t.parent(c) == a && t.key(c) == 3 && add(ob) == b && t.size() == before + 1;
    return ok;
}
static_assert(trie_laws());

// ── id_set laws ────────────────────────────────────────────────────────────
template <class Set>
consteval bool id_set_laws() {
    Set s;
    bool ok = s.empty() && !s.has(0) && !s.has(100000);
    ok = ok && s.note(5) && s.note(70) && !s.note(5) && s.size() == 2;          // note dedups
    ok = ok && s.has(5) && s.has(70) && !s.has(6) && s[0] == 5 && s[1] == 70;    // insertion order kept
    Set o;
    ok = ok && o.note(70) && o.note(200);
    const Set u = s.united(o);
    const Set i = s.intersected(o);
    const Set d = s.subtracted(o);
    ok = ok && u.size() == 3 && u[0] == 5 && u[1] == 70 && u[2] == 200;         // mine, then theirs
    ok = ok && i.size() == 1 && i[0] == 70 && d.size() == 1 && d[0] == 5;
    const Set w = u.where([](typename Set::id_type id) { return id > 10; });
    ok = ok && w.size() == 2 && w[0] == 70 && w[1] == 200 && !w.has(5) && w.has(200);
    ok = ok && s.sibling().empty() && s.bytes() > 0;
    // counting without building: agrees with the built sets, including when
    // one bitmap is wider than the other (ids 5, 70 vs 70, 200, 500)
    ok = ok && o.note(500);
    ok = ok && s.count_united(o) == s.united(o).size() && o.count_united(s) == o.united(s).size();
    ok = ok && s.count_intersected(o) == s.intersected(o).size() && o.count_intersected(s) == 1;
    ok = ok && s.count_subtracted(o) == s.subtracted(o).size() && o.count_subtracted(s) == 2;
    ok = ok && Set{}.count_united(o) == 3 && o.count_intersected(Set{}) == 0 && o.count_subtracted(Set{}) == 3;
    return ok;
}
static_assert(id_set_laws<id_set>());                                      // the default: 32-bit ids, 64-bit words
static_assert(id_set_laws<basic_id_set<std::uint16_t, std::uint32_t>>());    // narrow ids, 32-bit words: the same laws, word boundaries at 32
static_assert(id_set_laws<basic_id_set<std::uint64_t, std::uint64_t>>());    // 64-bit ids
static_assert(id_set::word_bits == 64 && basic_id_set<std::uint16_t, std::uint32_t>::word_bits == 32);

}  // namespace
