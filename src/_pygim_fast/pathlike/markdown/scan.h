#pragma once
// pathlike/markdown/scan.h — where the inline parser must stop: one bit per byte.
//
// The inline parser reads text in runs: everything up to the next byte that
// can start markup is plain text. Those bytes are the STOPS — newline,
// backslash, backtick, `*`, `_`, `[`, `]`, `!`, `<`, `&` and `~` — and markdown
// has one every 13–19 bytes (measured over pygim's own documents, ENACT #130).
// A stop index is built once per inline block, 64 bytes per word, and the
// parser jumps from stop to stop with a count-trailing-zeros (simdjson's
// stage 1, with markdown's stops):
//
//     text   "a *b* `c`"          stops at 2, 4, 6, 8
//     words  [0b1_0101_0100]      bit i set <=> text[i] is a stop
//     next(0) -> 2, next(3) -> 4, next(9) -> npos
//
// Building the index is a policy, chosen per platform at compile time — a
// complete, equivalent implementation each, never a run-time CPU check:
//
//     scalar_scan  a 256-entry table, one byte at a time; constexpr — the
//                  specification, and the only one constant evaluation runs
//     sse2_scan    16 bytes a compare, four folded into one 64-bit word
//                  (x86-64: SSE2 is in every x86-64 CPU)
//     neon_scan    the same on AArch64 (NEON is in every AArch64 CPU)
//
// Measured by benchmarks/markdown_parse.py over 19.5 MB of markdown (437
// files, a stop every 26 bytes; Ryzen 5800X, best of 5): scalar 14.2 ms
// (1,376 MB/s), SSE2 6.4 ms (3,069 MB/s) — 2.2x. AVX2 is not used: a probe
// (ENACT #130, not a recorded benchmark) found AVX2 at 32 bytes a word no
// faster than SSE2 at 64, and it would need a run-time CPU check.
// Every policy must produce the same words — tests/unittests/test_pathlike_markdown.py
// fuzzes them against scalar_scan through `markdown._stops`.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define PYGIM_MARKDOWN_SCAN_SSE2 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define PYGIM_MARKDOWN_SCAN_NEON 1
#endif

namespace pygim::pathlike::markdown {

/// The bytes that can start inline markup (GFM's `~` included; the CommonMark
/// dialect reads it as text).
inline constexpr std::string_view inline_stops = "\n\\`*_[]!<&~";

inline constexpr std::array<bool, 256> stop_table = [] {
    std::array<bool, 256> t{};
    for (char c : inline_stops) t[static_cast<unsigned char>(c)] = true;
    return t;
}();

/// One byte at a time through a table: the reference every other policy must equal.
struct scalar_scan {
    static constexpr std::string_view name = "scalar";
    /// Sets bit i of words for every stop at s[i]; words is zeroed and (n+63)/64 long.
    static constexpr void fill(std::string_view s, std::uint64_t* words) noexcept {
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (stop_table[static_cast<unsigned char>(s[i])]) words[i / 64] |= std::uint64_t{1} << (i % 64);
        }
    }
};

#if PYGIM_MARKDOWN_SCAN_SSE2
/// SSE2: one compare per stop byte over 16 bytes, four 16-bit masks folded into a word.
struct sse2_scan {
    static constexpr std::string_view name = "sse2";
    static std::uint64_t mask16(const char* p) noexcept {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i m = _mm_cmpeq_epi8(v, _mm_set1_epi8('\n'));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('\\')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('`')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('*')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('_')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('[')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8(']')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('!')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('<')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('&')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('~')));
        return static_cast<std::uint32_t>(_mm_movemask_epi8(m));
    }
    static void fill(std::string_view s, std::uint64_t* words) noexcept {
        const char* p = s.data();
        const std::size_t n = s.size();
        std::size_t i = 0;
        for (; i + 64 <= n; i += 64) {
            words[i / 64] = mask16(p + i) | mask16(p + i + 16) << 16 | mask16(p + i + 32) << 32 | mask16(p + i + 48) << 48;
        }
        for (; i < n; ++i) {
            if (stop_table[static_cast<unsigned char>(p[i])]) words[i / 64] |= std::uint64_t{1} << (i % 64);
        }
    }
};
using simd_scan = sse2_scan;
#elif PYGIM_MARKDOWN_SCAN_NEON
/// NEON: the same compares; a 64-bit word from four 16-byte masks by pairwise
/// adds of per-lane bit weights (simdjson's arm64 movemask).
struct neon_scan {
    static constexpr std::string_view name = "neon";
    static uint8x16_t mask16(const char* p) noexcept {
        const uint8x16_t v = vld1q_u8(reinterpret_cast<const std::uint8_t*>(p));
        uint8x16_t m = vceqq_u8(v, vdupq_n_u8('\n'));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('\\')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('`')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('*')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('_')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('[')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8(']')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('!')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('<')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('&')));
        m = vorrq_u8(m, vceqq_u8(v, vdupq_n_u8('~')));
        return m;
    }
    static std::uint64_t word(const char* p) noexcept {
        static constexpr std::uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        const uint8x16_t w = vld1q_u8(weights);
        const uint8x16_t t0 = vandq_u8(mask16(p), w);
        const uint8x16_t t1 = vandq_u8(mask16(p + 16), w);
        const uint8x16_t t2 = vandq_u8(mask16(p + 32), w);
        const uint8x16_t t3 = vandq_u8(mask16(p + 48), w);
        uint8x16_t sum = vpaddq_u8(vpaddq_u8(t0, t1), vpaddq_u8(t2, t3));
        sum = vpaddq_u8(sum, sum);
        return vgetq_lane_u64(vreinterpretq_u64_u8(sum), 0);
    }
    static void fill(std::string_view s, std::uint64_t* words) noexcept {
        const char* p = s.data();
        const std::size_t n = s.size();
        std::size_t i = 0;
        for (; i + 64 <= n; i += 64) words[i / 64] = word(p + i);
        for (; i < n; ++i) {
            if (stop_table[static_cast<unsigned char>(p[i])]) words[i / 64] |= std::uint64_t{1} << (i % 64);
        }
    }
};
using simd_scan = neon_scan;
#else
using simd_scan = scalar_scan;   // a platform with neither: the reference itself
#endif

/// The scan pygim builds with: the platform's SIMD policy.
using default_scan = simd_scan;

/// The stops of one text, as bits; `next(i)` is the first stop at or after i.
///
///     basic_stop_index<scalar_scan> ix("a *b*");   ix.next(0) -> 2, ix.next(3) -> 4, ix.next(5) -> npos
///
/// Cost: one pass over the text when built (Scan::fill), then a word load and
/// a count-trailing-zeros per call. Owns its words; does not keep the text.
template <class Scan = default_scan>
class basic_stop_index {
public:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    constexpr basic_stop_index() = default;
    constexpr explicit basic_stop_index(std::string_view s) : m_size(s.size()), m_words((s.size() + 63) / 64, 0) {
        if consteval {
            scalar_scan::fill(s, m_words.data());
        } else {
            Scan::fill(s, m_words.data());
        }
    }

    /// The first stop at or after `i`, or npos.
    [[nodiscard]] constexpr std::size_t next(std::size_t i) const noexcept {
        if (i >= m_size) return npos;
        std::size_t w = i / 64;
        std::uint64_t bits = m_words[w] & (~std::uint64_t{0} << (i % 64));
        while (bits == 0) {
            if (++w == m_words.size()) return npos;
            bits = m_words[w];
        }
        const std::size_t at = w * 64 + static_cast<std::size_t>(std::countr_zero(bits));
        return at < m_size ? at : npos;
    }

    /// The raw words, for tests comparing policies.
    [[nodiscard]] constexpr const std::vector<std::uint64_t>& words() const noexcept { return m_words; }

private:
    std::size_t m_size = 0;
    std::vector<std::uint64_t> m_words;
};

using stop_index = basic_stop_index<>;

}  // namespace pygim::pathlike::markdown
