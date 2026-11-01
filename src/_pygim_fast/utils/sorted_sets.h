#pragma once
// utils/sorted_sets.h — how many values two sorted sets of 64-bit integers have in common.
//
// CORE layer: pybind-free. Both inputs ascending, each without repeats (a multiset becomes a set by
// giving each repeat a value of its own: similarity's fingerprints do, so Σ min(count) of two
// multisets is the size of their sets' intersection).
//
//     const std::uint64_t a[] = {1, 4, 9, 12}, b[] = {4, 5, 12};
//     intersection_size(a, 4, b, 3)     -> 2   (4 and 12)
//
// Three ways, one answer: a binary search from where the last one was found ("galloping") when
// one set is more than 16 times the other; otherwise a merge without a branch per step (two
// random hashes compare unpredictably) — the definition, which constant evaluation, inputs under
// four values and machines without AVX2 run; and on x86-64 with AVX2 (chosen at run time,
// utils/cpu.h), four values of each side compared at once, every rotation of one against the
// other, the side whose fourth value is lower stepping on (docs/design/vectorisation.md, tier 3).
// Measured 2026-10-27 on the candidate pairs of similarity's ranked queries (Ryzen 7 5800X, one
// thread): CPython's Lib/test at 0.5, 12.7M pairs of 30 values together, 726-741 ms merged,
// 493-515 ms with AVX2; LLVM at 0.5, 5.4M pairs of 15, 170-175 ms against 138-145.
//
// PYGIM_SORTED_SETS_SCALAR builds the merge alone.

#include <cstddef>
#include <cstdint>

#include "cpu.h"

#if !defined(PYGIM_SORTED_SETS_SCALAR) && (defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)) && \
    (defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER))
#define PYGIM_SORTED_SETS_AVX2 1
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#endif

namespace pygim::sorted_sets {

namespace detail {

// The definition: one step of either side or both per pair of values looked at.
[[nodiscard]] constexpr std::size_t merged(const std::uint64_t* a, std::size_t na, const std::uint64_t* b, std::size_t nb) noexcept {
    std::size_t s = 0, i = 0, j = 0;
    while (i < na && j < nb) {
        const std::uint64_t u = a[i], v = b[j];
        s += u == v;
        i += u <= v;
        j += v <= u;
    }
    return s;
}
static_assert([] {
    const std::uint64_t a[] = {1, 4, 9}, b[] = {4, 9, 12};
    return merged(a, 3, b, 3) == 2;
}());

// For |large| far above |small|: each of `small` found by a binary search from where the last was.
[[nodiscard]] constexpr std::size_t galloped(const std::uint64_t* small, std::size_t ns, const std::uint64_t* large, std::size_t nl) noexcept {
    std::size_t s = 0, at = 0;
    for (std::size_t i = 0; i < ns && at < nl; ++i) {
        std::size_t step = 1, lo = at;
        while (lo + step < nl && large[lo + step] < small[i]) {
            lo += step;
            step *= 2;
        }
        std::size_t hi = lo + step + 1 < nl ? lo + step + 1 : nl;
        while (lo < hi) {   // the first at or past small[i]
            const std::size_t mid = lo + (hi - lo) / 2;
            if (large[mid] < small[i]) lo = mid + 1;
            else hi = mid;
        }
        at = lo;
        if (at < nl && large[at] == small[i]) {
            ++s;
            ++at;
        }
    }
    return s;
}
static_assert([] {
    const std::uint64_t small[] = {3, 40}, large[] = {1, 2, 3, 5, 8, 13, 21, 34, 40, 55};
    return galloped(small, 2, large, 10) == 2;
}());

/// How many times the larger set may hold the smaller before a gallop beats the merge.
inline constexpr std::size_t gallop_from = 16;

#if PYGIM_SORTED_SETS_AVX2
#if defined(__GNUC__) || defined(__clang__)
#define PYGIM_SORTED_SETS_TARGET __attribute__((target("avx2,popcnt")))
#else
#define PYGIM_SORTED_SETS_TARGET
#endif
// Four of each side at once: a equal to b, or to b rotated by one, two or three places, is a value
// in common (no repeats, so each counts once); then the side with the lower fourth value steps on
// by four (both when they are equal). The rest goes to the merge.
PYGIM_SORTED_SETS_TARGET inline std::size_t merged_avx2(const std::uint64_t* a, std::size_t na, const std::uint64_t* b, std::size_t nb) noexcept {
    std::size_t s = 0, i = 0, j = 0;
    while (i + 4 <= na && j + 4 <= nb) {
        const __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
        const __m256i y = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + j));
        __m256i eq = _mm256_cmpeq_epi64(x, y);
        eq = _mm256_or_si256(eq, _mm256_cmpeq_epi64(x, _mm256_permute4x64_epi64(y, 0x39)));
        eq = _mm256_or_si256(eq, _mm256_cmpeq_epi64(x, _mm256_permute4x64_epi64(y, 0x4e)));
        eq = _mm256_or_si256(eq, _mm256_cmpeq_epi64(x, _mm256_permute4x64_epi64(y, 0x93)));
        const auto bits = static_cast<unsigned>(_mm256_movemask_pd(_mm256_castsi256_pd(eq)));
#if defined(_MSC_VER) && !defined(__clang__)
        s += __popcnt(bits);
#else
        s += static_cast<std::size_t>(__builtin_popcount(bits));
#endif
        const std::uint64_t last_a = a[i + 3], last_b = b[j + 3];
        i += last_a <= last_b ? 4 : 0;
        j += last_b <= last_a ? 4 : 0;
    }
    return s + merged(a + i, na - i, b + j, nb - j);
}
#undef PYGIM_SORTED_SETS_TARGET
#endif

}  // namespace detail

/// How many values the ascending, repeat-free sets a[0..na) and b[0..nb) have in common.
/// Cost: O(na + nb), or O(small · log large) when one is more than detail::gallop_from times the
/// other; no allocation.
[[nodiscard]] constexpr std::size_t intersection_size(const std::uint64_t* a, std::size_t na, const std::uint64_t* b, std::size_t nb) noexcept {
    if (na * detail::gallop_from < nb) return detail::galloped(a, na, b, nb);
    if (nb * detail::gallop_from < na) return detail::galloped(b, nb, a, na);
#if PYGIM_SORTED_SETS_AVX2
    if !consteval {
        if (na >= 4 && nb >= 4 && has_avx2()) return detail::merged_avx2(a, na, b, nb);
    }
#endif
    return detail::merged(a, na, b, nb);
}
static_assert([] {   // the example above
    const std::uint64_t a[] = {1, 4, 9, 12}, b[] = {4, 5, 12};
    return intersection_size(a, 4, b, 3) == 2;
}());

}  // namespace pygim::sorted_sets
