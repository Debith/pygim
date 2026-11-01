#pragma once
// utils/byte_scan.h — the bytes of a text that stop a scan: one not ASCII, a line end.
//
// CORE layer: pybind-free. What a reader does once over a whole buffer before it reads it (check
// that a file's text is UTF-8, find where its lines start) runs 64 bytes a step here: SSE2 on
// every x86-64 (four 16-byte compares folded into one 64-bit mask), NEON on every arm64, and on
// x86-64 with AVX2 two 32-byte compares, chosen at run time (utils/cpu.h). The first set bit of a
// mask is the next stop; for_each_line_end takes every set bit of each mask in turn, so a whole
// text costs one mask per 64 bytes, not one search per line (#130).
//
//     first_non_ascii("abc\xc3\xa9")          -> 3
//     first_line_end("ab\r\ncd")              -> 2      first_line_end("ab\r\ncd", 3) -> 3
//     first_line_end("abc")                   -> 3      (none: the size)
//     for_each_line_end("a\nb\r\n", f)        -> f(1), f(3), f(4)
//
// The scalar loop is the definition: a constant evaluation, the tail under one step, and a build
// with -DPYGIM_BYTE_SCAN_SCALAR run it alone. Every path is checked against it at every offset
// around its steps, each called directly, since the dispatch runs one a machine
// (tests/static/run_byte_scan_paths.py), and the dispatched one against CPython's positions
// (test_code_parse.py, test_the_buffer_scans_agree_with_cpython_at_every_offset_around_their_steps:
// a non-ASCII character and each kind of line end at every offset 0-140). Measured on CPython's Lib (36 MB, 972,865 lines), one thread,
// 2026-10-27: the line index one search per line 12.6-13.5 ms, one mask per 64 bytes 5.0-5.6 ms
// with SSE2, 3.9-4.4 ms with AVX2; ASCII runs 1.7-2.2 ms with SSE2, 1.1-1.7 ms with AVX2.
// A lexer gains little from wider scans (17% alone, nothing in a whole parse: its runs are short).

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cpu.h"

#if !defined(PYGIM_BYTE_SCAN_SCALAR) && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64))
#define PYGIM_BYTE_SCAN_SSE2 1
#include <emmintrin.h>
#if (defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)) && (defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER))
#define PYGIM_BYTE_SCAN_AVX2 1
#include <immintrin.h>
#endif
#elif !defined(PYGIM_BYTE_SCAN_SCALAR) && (defined(__aarch64__) || defined(_M_ARM64))
#define PYGIM_BYTE_SCAN_NEON 1
#include <arm_neon.h>
#endif

#if PYGIM_BYTE_SCAN_AVX2 && (defined(__GNUC__) || defined(__clang__))
#define PYGIM_BYTE_SCAN_AVX2_TARGET __attribute__((target("avx2")))
#else
#define PYGIM_BYTE_SCAN_AVX2_TARGET
#endif

namespace pygim::byte_scan {

namespace detail {

// What stops a scan, as a policy type: whether a byte does (the definition), and which bytes of
// 16 (SSE2, NEON) or 32 (AVX2) do at once, as a vector whose bytes' high bits say so. A stop is
// one class; every mask, search and walk below is written once for all of them.
class not_ascii {
public:
    [[nodiscard]] static constexpr bool stops(unsigned char c) noexcept { return c >= 0x80; }
#if PYGIM_BYTE_SCAN_SSE2
    [[nodiscard]] static __m128i marks(__m128i v) noexcept { return v; }   // its own high bit
#endif
#if PYGIM_BYTE_SCAN_AVX2
    [[nodiscard]] PYGIM_BYTE_SCAN_AVX2_TARGET static __m256i marks(__m256i v) noexcept { return v; }
#endif
#if PYGIM_BYTE_SCAN_NEON
    [[nodiscard]] static uint8x16_t marks(uint8x16_t v) noexcept { return vcgeq_u8(v, vdupq_n_u8(0x80)); }
#endif
};
static_assert(not_ascii::stops(0x80) && !not_ascii::stops('a'));

class line_end {
public:
    [[nodiscard]] static constexpr bool stops(unsigned char c) noexcept { return c == '\n' || c == '\r'; }
#if PYGIM_BYTE_SCAN_SSE2
    [[nodiscard]] static __m128i marks(__m128i v) noexcept { return _mm_or_si128(_mm_cmpeq_epi8(v, _mm_set1_epi8('\n')), _mm_cmpeq_epi8(v, _mm_set1_epi8('\r'))); }
#endif
#if PYGIM_BYTE_SCAN_AVX2
    [[nodiscard]] PYGIM_BYTE_SCAN_AVX2_TARGET static __m256i marks(__m256i v) noexcept {
        return _mm256_or_si256(_mm256_cmpeq_epi8(v, _mm256_set1_epi8('\n')), _mm256_cmpeq_epi8(v, _mm256_set1_epi8('\r')));
    }
#endif
#if PYGIM_BYTE_SCAN_NEON
    [[nodiscard]] static uint8x16_t marks(uint8x16_t v) noexcept { return vorrq_u8(vceqq_u8(v, vdupq_n_u8('\n')), vceqq_u8(v, vdupq_n_u8('\r'))); }
#endif
};
static_assert(line_end::stops('\r') && line_end::stops('\n') && !line_end::stops(' '));

template <class S>
concept stop = requires(unsigned char c) {
    { S::stops(c) } -> std::same_as<bool>;
};

// The definition: the first byte from `i` that stops, or the size.
template <stop S>
[[nodiscard]] constexpr std::size_t scalar(std::string_view s, std::size_t i) noexcept {
    while (i < s.size() && !S::stops(static_cast<unsigned char>(s[i]))) ++i;
    return i;
}
static_assert(scalar<not_ascii>("ab\xc3", 0) == 2 && scalar<not_ascii>("ab", 0) == 2 && scalar<line_end>("a\rb", 0) == 1);

// Each loop below steps while a whole step is left (`n - i >= step`, which cannot wrap: i <= n).

#if PYGIM_BYTE_SCAN_SSE2
// 16 bytes from p: bit k set where byte k stops.
template <stop S>
[[nodiscard]] inline std::uint64_t mask16(const char* p) noexcept {
    return static_cast<std::uint32_t>(_mm_movemask_epi8(S::marks(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)))));
}
template <stop S>
[[nodiscard]] inline std::uint64_t mask64(const char* p) noexcept {
    return mask16<S>(p) | mask16<S>(p + 16) << 16 | mask16<S>(p + 32) << 32 | mask16<S>(p + 48) << 48;
}
// The first stop from i, or the first offset from which fewer than 16 bytes are left.
template <stop S>
[[nodiscard]] inline std::size_t first_wide(const char* p, std::size_t i, std::size_t n) noexcept {
    for (; n - i >= 64; i += 64) {
        if (const std::uint64_t m = mask64<S>(p + i); m != 0) return i + static_cast<std::size_t>(std::countr_zero(m));
    }
    for (; n - i >= 16; i += 16) {
        if (const std::uint64_t m = mask16<S>(p + i); m != 0) return i + static_cast<std::size_t>(std::countr_zero(m));
    }
    return i;
}
// f(offset) for every stop below the last whole 64 bytes; returns where it ended.
template <stop S, class F>
inline std::size_t each_wide(const char* p, std::size_t n, F& f) {
    std::size_t i = 0;
    for (; n - i >= 64; i += 64) {
        for (std::uint64_t m = mask64<S>(p + i); m != 0; m &= m - 1) f(i + static_cast<std::size_t>(std::countr_zero(m)));
    }
    return i;
}
#endif

#if PYGIM_BYTE_SCAN_AVX2
// As the SSE2 ones, two 32-byte compares a step; they run only where has_avx2().
template <stop S>
PYGIM_BYTE_SCAN_AVX2_TARGET inline std::uint64_t mask64_avx2(const char* p) noexcept {
    const auto lo = static_cast<std::uint32_t>(_mm256_movemask_epi8(S::marks(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)))));
    const auto hi = static_cast<std::uint32_t>(_mm256_movemask_epi8(S::marks(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32)))));
    return lo | static_cast<std::uint64_t>(hi) << 32;
}
// The first stop from i, or the first offset from which fewer than 64 bytes are left.
template <stop S>
PYGIM_BYTE_SCAN_AVX2_TARGET inline std::size_t first_avx2(const char* p, std::size_t i, std::size_t n) noexcept {
    for (; n - i >= 64; i += 64) {
        if (const std::uint64_t m = mask64_avx2<S>(p + i); m != 0) return i + static_cast<std::size_t>(std::countr_zero(m));
    }
    return i;
}
template <stop S, class F>
PYGIM_BYTE_SCAN_AVX2_TARGET inline std::size_t each_avx2(const char* p, std::size_t n, F& f) {
    std::size_t i = 0;
    for (; n - i >= 64; i += 64) {
        for (std::uint64_t m = mask64_avx2<S>(p + i); m != 0; m &= m - 1) f(i + static_cast<std::size_t>(std::countr_zero(m)));
    }
    return i;
}
#endif

#if PYGIM_BYTE_SCAN_NEON
// NEON has no movemask: a 16-byte compare narrows to 4 bits a byte (shrn), and a 64-byte step
// first asks whether any byte stops at all (umaxv).
template <stop S>
[[nodiscard]] inline uint8x16_t compare16(const char* p) noexcept { return S::marks(vld1q_u8(reinterpret_cast<const std::uint8_t*>(p))); }
[[nodiscard]] inline std::uint64_t nibbles(uint8x16_t m) noexcept {
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(m), 4)), 0);
}
template <stop S>
[[nodiscard]] inline std::size_t first_wide(const char* p, std::size_t i, std::size_t n) noexcept {
    for (; n - i >= 64; i += 64) {
        const uint8x16_t c[4] = {compare16<S>(p + i), compare16<S>(p + i + 16), compare16<S>(p + i + 32), compare16<S>(p + i + 48)};
        if (vmaxvq_u8(vorrq_u8(vorrq_u8(c[0], c[1]), vorrq_u8(c[2], c[3]))) == 0) continue;
        for (std::size_t k = 0; k < 4; ++k) {
            if (const std::uint64_t m = nibbles(c[k]); m != 0) return i + 16 * k + static_cast<std::size_t>(std::countr_zero(m)) / 4;
        }
    }
    for (; n - i >= 16; i += 16) {
        if (const std::uint64_t m = nibbles(compare16<S>(p + i)); m != 0) return i + static_cast<std::size_t>(std::countr_zero(m)) / 4;
    }
    return i;
}
template <stop S, class F>
inline std::size_t each_wide(const char* p, std::size_t n, F& f) {
    std::size_t i = 0;
    for (; n - i >= 64; i += 64) {
        const uint8x16_t c[4] = {compare16<S>(p + i), compare16<S>(p + i + 16), compare16<S>(p + i + 32), compare16<S>(p + i + 48)};
        if (vmaxvq_u8(vorrq_u8(vorrq_u8(c[0], c[1]), vorrq_u8(c[2], c[3]))) == 0) continue;
        for (std::size_t k = 0; k < 4; ++k) {
            for (std::uint64_t m = nibbles(c[k]); m != 0;) {
                const auto bit = static_cast<std::size_t>(std::countr_zero(m));
                f(i + 16 * k + bit / 4);
                m &= ~(std::uint64_t{0xF} << (bit & ~std::size_t{3}));   // the byte's four bits
            }
        }
    }
    return i;
}
#endif

// The first stop at or after `from` (the size for a `from` past it): the widest scan this machine
// runs, then the definition. A stop the AVX2 loop finds is the answer; only its tail goes on.
template <stop S>
[[nodiscard]] constexpr std::size_t first(std::string_view s, std::size_t from) noexcept {
    if (from >= s.size()) return s.size();
#if PYGIM_BYTE_SCAN_SSE2 || PYGIM_BYTE_SCAN_NEON
    if !consteval {
#if PYGIM_BYTE_SCAN_AVX2
        if (has_avx2()) {
            from = first_avx2<S>(s.data(), from, s.size());
            if (from < s.size() && S::stops(static_cast<unsigned char>(s[from]))) return from;
        }
#endif
        from = first_wide<S>(s.data(), from, s.size());
    }
#endif
    return scalar<S>(s, from);
}
static_assert(first<line_end>("ab\ncd", 1) == 2 && first<line_end>("ab", 7) == 2);

}  // namespace detail

/// The offset of the first byte of `s` from `from` that is 0x80 or above, or s.size().
[[nodiscard]] constexpr std::size_t first_non_ascii(std::string_view s, std::size_t from = 0) noexcept {
    return detail::first<detail::not_ascii>(s, from);
}
static_assert(first_non_ascii("abc\xc3\xa9") == 3 && first_non_ascii("abc") == 3 && first_non_ascii("\x80") == 0 &&
              first_non_ascii("\x80" "a", 1) == 2);

/// The offset of the first '\n' or '\r' of `s` from `from`, or s.size().
[[nodiscard]] constexpr std::size_t first_line_end(std::string_view s, std::size_t from = 0) noexcept {
    return detail::first<detail::line_end>(s, from);
}
static_assert(first_line_end("ab\r\ncd") == 2 && first_line_end("ab\r\ncd", 3) == 3 && first_line_end("abc") == 3);

/// f(offset) for every '\n' and '\r' of `s`, in order: one mask per 64 bytes, every stop taken
/// from it, where a search per line would load the 64 bytes around each.
template <class F>
constexpr void for_each_line_end(std::string_view s, F&& f) {
    std::size_t i = 0;
#if PYGIM_BYTE_SCAN_SSE2 || PYGIM_BYTE_SCAN_NEON
    if !consteval {
#if PYGIM_BYTE_SCAN_AVX2
        if (has_avx2()) i = detail::each_avx2<detail::line_end>(s.data(), s.size(), f);
        else
#endif
            i = detail::each_wide<detail::line_end>(s.data(), s.size(), f);
    }
#endif
    for (i = detail::scalar<detail::line_end>(s, i); i < s.size(); i = detail::scalar<detail::line_end>(s, i + 1)) f(i);
}
static_assert([] {   // the example above
    std::size_t got[3] = {}, k = 0;
    for_each_line_end("a\nb\r\n", [&](std::size_t at) { got[k++] = at; });
    return k == 3 && got[0] == 1 && got[1] == 3 && got[2] == 4;
}());

}  // namespace pygim::byte_scan
