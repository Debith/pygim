#pragma once
// utils/hash.h — the hashing primitives the core headers share.
//
// CORE layer: pybind-free, constexpr throughout, so the same functions serve
// runtime tables and compile-time proofs. One definition each: the value hash
// of basic_file (pathlike/core.h) and of a path_table row (mapping/trie.h +
// pathlike/path_table.h) are the same FNV-1a stream, which is what lets a view
// into a table hash like the file with the same value.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pygim::hash {

// FNV-1a over bytes.
inline constexpr std::uint64_t fnv_basis = 14695981039346656037ull;
inline constexpr std::uint64_t fnv_prime = 1099511628211ull;
[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view s, std::uint64_t h = fnv_basis) noexcept {
    for (const unsigned char c : s) {
        h ^= c;
        h *= fnv_prime;
    }
    return h;
}

// One string of a composite value: its bytes, then a terminator so that
// ("ab","c") and ("a","bc") differ.
[[nodiscard]] constexpr std::uint64_t mix_string(std::uint64_t h, std::string_view s) noexcept {
    h = fnv1a(s, h);
    h ^= 0xffu;
    h *= fnv_prime;
    return h;
}

// splitmix64 finaliser: spreads a 64-bit key over the low bits used as a slot index.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

// Two hashes into one (the boost::hash_combine recipe): order-sensitive and
// not the xor-shift that collapses (a, b) and (b, a) onto each other.
[[nodiscard]] constexpr std::size_t combine(std::size_t h1, std::size_t h2) noexcept {
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
}

// The power of two that keeps `n` keys in an open-addressing table at load
// factor <= 1/2 (never below 64 slots).
[[nodiscard]] constexpr std::size_t slots_for(std::size_t n) noexcept {
    std::size_t s = 64;
    while (s < n * 2) s *= 2;
    return s;
}

}  // namespace pygim::hash
