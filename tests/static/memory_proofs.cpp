// tests/static/memory_proofs.cpp — compile-time proofs of the memory core's laws.
//
// Compiled into pygim.memory (ext.memory.toml), so a build that breaks a law
// does not link. The laws are docs/design/memory/01_domain_model.md §8 and
// 02 §6; each is proven over more than one instantiation where it has one.
#include <array>
#include <cstdint>
#include <string_view>

#include "../../src/_pygim_fast/mapping/id_set.h"
#include "../../src/_pygim_fast/memory/core/ids.h"
#include "../../src/_pygim_fast/memory/core/knowledge.h"

namespace {

using namespace pygim::memory;

// ── strong ids: distinct, defaulted to invalid, totally ordered ─────────────
static_assert(!memory_id{}.valid() && memory_id{2}.valid());
static_assert(memory_id{2} < memory_id{3} && tag_id{7} == tag_id{7});
static_assert(!std::is_convertible_v<tag_id, memory_id>, "ids of different kinds never mix");

// ── exact weights (02 §1.4): the digits, never a float ──────────────────────
static_assert(parse_weight("1.5") == 1500 && parse_weight("2") == 2000 && parse_weight("2.0") == 2000);
static_assert(parse_weight("0.5") == 500 && parse_weight("0.125") == 125 && parse_weight("1000") == 1000000);
static_assert(!parse_weight("0.0001") && !parse_weight("-1") && !parse_weight("1e3") && !parse_weight("0"));
static_assert(!parse_weight("") && !parse_weight(".") && !parse_weight("1000.001") && !parse_weight("1.5x"));

constexpr bool weights_round_trip() {
    constexpr std::array<weight_t, 9> ws{1, 5, 50, 125, 500, 1000, 1500, 2000, 999999};
    for (const auto w : ws)
        if (parse_weight(weight_text(w)) != w) return false;
    return weight_text(1500) == "1.5" && weight_text(2000) == "2.0" && weight_text(125) == "0.125";
}
static_assert(weights_round_trip(), "every weight prints as text that parses back to it");

// ── the digest: deterministic, round-trips, and different inputs differ ─────
template <class D>
constexpr bool digest_laws() {
    constexpr std::array<std::string_view, 8> inputs{"", "a", "b", "ab", "ba", "abc", "abcdefgh", "abcdefghi"};
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const D d = D::of(inputs[i]);
        if (d != D::of(inputs[i])) return false;             // deterministic
        if (D::from_hex(d.hex()) != d) return false;          // round trip
        for (std::size_t j = i + 1; j < inputs.size(); ++j)
            if (D::of(inputs[j]) == d) return false;          // distinct
    }
    return !D::from_hex("xyz") && D{}.empty() && !D::of("").empty();
}
static_assert(digest_laws<digest>());
static_assert(digest_laws<basic_digest<64>>());
static_assert(digest_laws<basic_digest<256>>());

// ── canonical encoding is unambiguous ────────────────────────────────────────
constexpr bool fields_unambiguous() {
    std::string a, b;
    encode_field(a, "ab");
    encode_field(a, "c");
    encode_field(b, "a");
    encode_field(b, "bc");
    return a != b && a == "2:ab1:c";
}
static_assert(fields_unambiguous(), "(\"ab\",\"c\") and (\"a\",\"bc\") encode differently");

// ── token estimates (04 §3.7) ────────────────────────────────────────────────
static_assert(token_estimate(0) == 1 && token_estimate(7) == 1 && token_estimate(8) == 2 && token_estimate(400) == 100);

// ── candidate algebra (04 §6): union within a dimension, intersection across,
//    equals the rule "every hard dimension answered by one of the query's values"
template <class Id>
constexpr bool candidate_algebra() {
    using set = pygim::mapping::basic_id_set<Id>;
    // five memories; dimension A has values a0 a1, dimension B has b0 b1
    //   m0: a0 b0   m1: a1 b0   m2: a0 b1   m3: a1   m4: b1
    constexpr std::array<std::array<bool, 4>, 5> carries{{
        {true, false, true, false}, {false, true, true, false}, {true, false, false, true},
        {false, true, false, false}, {false, false, false, true}}};
    std::array<set, 4> postings{};
    for (std::size_t m = 0; m < carries.size(); ++m)
        for (std::size_t t = 0; t < 4; ++t)
            if (carries[m][t]) postings[t].note(static_cast<Id>(m));
    // every hard query: a non-empty subset of {a0,a1} and of {b0,b1}
    for (unsigned qa = 1; qa < 4; ++qa)
        for (unsigned qb = 1; qb < 4; ++qb) {
            set ua, ub;
            for (unsigned v = 0; v < 2; ++v) {
                if (qa & (1u << v)) ua = ua.united(postings[v]);
                if (qb & (1u << v)) ub = ub.united(postings[2 + v]);
            }
            const set cand = ua.intersected(ub);
            for (std::size_t m = 0; m < carries.size(); ++m) {
                const bool a = ((qa & 1u) && carries[m][0]) || ((qa & 2u) && carries[m][1]);
                const bool b = ((qb & 1u) && carries[m][2]) || ((qb & 2u) && carries[m][3]);
                if (cand.has(static_cast<Id>(m)) != (a && b)) return false;
            }
        }
    return true;
}
static_assert(candidate_algebra<std::uint32_t>());
static_assert(candidate_algebra<std::uint16_t>());

// ── exact scores: a sum of integer weights is the same in any order ─────────
constexpr bool score_order_free() {
    constexpr std::array<weight_t, 4> w{2000, 1500, 1000, 1000};
    score_t forward = 0, backward = 0;
    for (std::size_t i = 0; i < w.size(); ++i) forward += w[i];
    for (std::size_t i = w.size(); i-- > 0;) backward += w[i];
    return forward == backward && forward * 1'000'000 == 5'500'000'000;
}
static_assert(score_order_free());

}  // namespace
