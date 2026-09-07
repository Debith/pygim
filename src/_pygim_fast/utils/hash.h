#pragma once
// utils/hash.h — the hashing primitives the core headers share.
//
// CORE layer: pybind-free, constexpr throughout, so the same functions serve
// runtime tables and compile-time proofs. One definition each: the value hash
// of basic_file (pathlike/core.h) and of a path_table row (mapping/trie.h +
// pathlike/path_table.h) are the same FNV-1a stream, which is what lets a view
// into a table hash like the file with the same value.
//
// Five functions, two jobs. `fnv1a` and `mix_string` turn bytes into a 64-bit
// value hash (what a string interner and a path compare by); `mix64`,
// `combine` and `slots_for` serve the open-addressing tables that store those
// values (mapping/intern.h, mapping/trie.h, the wiring registries): turning a
// hash into a slot index, folding two hashes into one, and sizing the table.
//
// A worked example, used in the comments below (every number computed with
// the 64-bit FNV-1a reference — pygim's own constants, not a library's):
//
//     fnv1a("")                        -> 14695981039346656037  (== fnv_basis)
//     fnv1a("a")                       -> 0xaf63dc4c8601ec8c
//     fnv1a("c", fnv1a("ab"))          -> == fnv1a("abc")       (a continued stream)
//
//     mix_string(mix_string(fnv_basis, "ab"), "c")  -> 0x20ba9b3025a8b421
//     mix_string(mix_string(fnv_basis, "a"), "bc")  -> 0xa0a3542c19b900ab   (differ)
//
//     (parent << 32 | key) & 63  for key 5, parents 0..3   -> 5, 5, 5, 5   (one slot)
//     mix64(parent << 32 | key) & 63, same keys             -> 28, 22, 49, 8
//
//     combine(1, 2)  -> 0x9e3779b97f4a7c56      combine(2, 1) -> 0x9e3779b97f4a7c94
//
//     slots_for(0) -> 64    slots_for(32) -> 64    slots_for(33) -> 128    slots_for(1000) -> 2048

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pygim::hash {

// ── value hashing ─────────────────────────────────────────────────────────

/// The 64-bit FNV-1a offset basis: the hash of nothing.   fnv1a("") == fnv_basis
inline constexpr std::uint64_t fnv_basis = 14695981039346656037ull;
/// The 64-bit FNV prime every byte is multiplied in with.
inline constexpr std::uint64_t fnv_prime = 1099511628211ull;
/// FNV-1a over the bytes of `s`, continuing from `h`: per byte, xor it in,
/// then multiply by the prime. Bytes are taken unsigned, so the same UTF-8
/// hashes the same whatever `char`'s signedness is on the platform.
///
///     fnv1a("")                  // 14695981039346656037 — no byte, the basis returned
///     fnv1a("a")                 // 0xaf63dc4c8601ec8c
///     fnv1a("c", fnv1a("ab"))    // the same value as fnv1a("abc")
///
/// The last line is the point of the `h` parameter: passing the previous
/// result continues ONE stream, so a value made of several strings can be
/// hashed piecewise (a path segment at a time, as path_table.h does walking
/// rows) and still agree with the hash of the whole. It is also why fnv1a
/// alone cannot tell ("ab","c") from ("a","bc") — see mix_string.
///
/// Cost: one xor and one 64-bit multiply per byte.
[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view s, std::uint64_t h = fnv_basis) noexcept {
    for (const unsigned char c : s) {
        h ^= c;
        h *= fnv_prime;
    }
    return h;
}

/// One string of a composite value folded into `h`: its bytes as fnv1a,
/// then a terminator byte (0xff) folded in the same way. The terminator is
/// what makes the split between strings part of the hash:
///
///     mix_string(mix_string(fnv_basis, "ab"), "c")   // 0x20ba9b3025a8b421
///     mix_string(mix_string(fnv_basis, "a"), "bc")   // 0xa0a3542c19b900ab
///
/// Without it both would be the stream a,b,c and hash alike (the fnv1a
/// example above); with it they are a,b,ff,c,ff and a,ff,b,c,ff. 0xff never
/// occurs in UTF-8 text, so for what this hashes — path segments, an
/// authority (pathlike/core.h, path_table.h) — the stream is unambiguous.
///
/// Cost: fnv1a over the string plus one more xor-multiply.
[[nodiscard]] constexpr std::uint64_t mix_string(std::uint64_t h, std::string_view s) noexcept {
    h = fnv1a(s, h);
    h ^= 0xffu;
    h *= fnv_prime;
    return h;
}

// ── table plumbing ────────────────────────────────────────────────────────

/// The splitmix64 finaliser: spreads every bit of a 64-bit key over the low
/// bits, which are what an open-addressing table keeps as the slot index
/// (`mix64(k) & (slots - 1)`). Needed because the keys the tables build are
/// structured, not random. The trie's child key is `parent << 32 | key`
/// (mapping/trie.h): its low bits are the child's key ALONE, so for 64 slots
///
///     (parent << 32 | 5) & 63           // 5, 5, 5, 5   for parents 0, 1, 2, 3
///     mix64(parent << 32 | 5) & 63      // 28, 22, 49, 8
///
/// — every child named "5" under every parent would land in one slot, and
/// keys 5 and 69 under the same parent too, since interner ids are dense
/// small integers. After mixing they scatter. The interner itself runs its
/// fnv1a value through this as well (mapping/intern.h), since FNV's low bits
/// are only as mixed as the last byte left them.
///
/// A bijection on 64-bit values (no two keys mix alike); mix64(0) == 0.
/// Cost: two multiplies and three shift-xors.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

/// Two hashes into one — the boost::hash_combine recipe — for a key made of
/// two parts hashed separately (a pointer and a name in the wiring
/// registries: wiring/registry/adapter.h, wiring/ioc/adapter.h).
///
///     combine(1, 2)   // 0x9e3779b97f4a7c56
///     combine(2, 1)   // 0x9e3779b97f4a7c94      (order matters)
///     combine(0, 0)   // 0x9e3779b97f4a7c15      (the golden-ratio constant, not 0)
///
/// What it fixes. The plain `h1 ^ h2` collapses (a, b) and (b, a) onto one
/// value — 1 ^ 2 == 2 ^ 1 == 3 — so a registry would confuse the key
/// (ptr=x, name=y) with (ptr=y, name=x) whenever the two parts hash to each
/// other's values, and (a, a) always to 0. The shifted `h1 ^ (h2 << 1)`
/// breaks that symmetry but stays linear: flipping bit i of h2 and bit i+1
/// of h1 cancels, so (1, 2) and (3, 3) both give 5, and bit 0 of the result
/// is bit 0 of h1 alone — h2's lowest bit never reaches the slot index.
/// Here h2 is shifted by a constant and by h1 in both directions, and the
/// sums carry upward, so no single bit flip on one side is undone by a
/// single flip on the other.
///
/// Cost: two shifts, three adds, one xor.
[[nodiscard]] constexpr std::size_t combine(std::size_t h1, std::size_t h2) noexcept {
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
}

/// The power of two that keeps `n` keys in an open-addressing table at load
/// factor <= 1/2, never below 64 slots — the size the interner and the trie
/// grow to when their key count passes half the slots (intern.h, trie.h).
///
///     slots_for(0)      // 64      (the floor)
///     slots_for(32)     // 64      (32 * 2 == 64, still fits)
///     slots_for(33)     // 128
///     slots_for(1000)   // 2048    (1024 would hold only 512 at this load)
///
/// A power of two so the slot index is `hash & (slots - 1)`; half empty so
/// linear probing stays a couple of steps on average.
/// Cost: one doubling per power of two above 64.
[[nodiscard]] constexpr std::size_t slots_for(std::size_t n) noexcept {
    std::size_t s = 64;
    while (s < n * 2) s *= 2;
    return s;
}

}  // namespace pygim::hash
