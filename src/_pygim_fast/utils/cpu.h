#pragma once
// utils/cpu.h — what this machine's processor offers a hot loop: its caches and its vector width.
//
// CORE layer: pybind-free. Read once, on first use: the caches from what the system reports
// (platform/caches.h), the vector width on x86-64 from the CPUID instruction. A size the system
// does not tell is a typical one (cache_layout::typical), never zero, so whatever is derived from it
// stays usable.
//
//     const cpu& here = this_cpu();
//     here.caches().l2()                 -> 524288 on a Ryzen 7 5800X (each core's own)
//     here.caches().l3()                 -> 33554432 there (shared by every core)
//     here.avx2()                        -> true there; false on arm64
//     has_avx2()                         -> the same, without reading the caches
//     tile = here.caches().l2() * 3 / 4 / row_bytes;   // what one worker writes stays in its L2
//
// Why a pipeline asks (docs/design/vectorisation.md, "Caches"): a worker that writes a block
// larger than its L2 sends it to memory, and a consumer on another core then reads it back from
// memory; blocks sized to the L2, few enough in flight to fit the shared L3, are handed over in
// cache. Measured 2026-10-27 on similarity's stream: 16K-pair tiles (384 KB) at 114 ms against
// 512K-pair blocks at 360 ms, for the same 127M pairs.

#include <cstddef>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
#  include <immintrin.h>
#  include <intrin.h>
#endif

#include "../platform/caches.h"

namespace pygim {

/// The data caches one core sees, in bytes: its own L1 and L2, the L3 its cores share, and the
/// line every transfer moves. Built from what a system reports; a 0 (not reported) is the typical
/// size, so no field is ever 0.
class cache_layout {
public:
    /// What a machine of today has when its system does not say: 32 KB L1, 512 KB L2, 8 MB L3, 64-byte lines.
    static constexpr std::size_t typical_l1d = 32 * 1024, typical_l2 = 512 * 1024, typical_l3 = 8 * 1024 * 1024, typical_line = 64;

    constexpr cache_layout() noexcept = default;
    constexpr cache_layout(std::size_t l1d, std::size_t l2, std::size_t l3, std::size_t line) noexcept
        : m_l1d(l1d ? l1d : typical_l1d), m_l2(l2 ? l2 : typical_l2), m_l3(l3 ? l3 : (m_l2 > typical_l3 ? m_l2 : typical_l3)),
          m_line(line ? line : typical_line) {}
    /// What a system reported, a 0 the typical size.
    constexpr explicit cache_layout(const platform::reported_caches& r) noexcept : cache_layout(r.l1d, r.l2, r.l3, r.line) {}

    [[nodiscard]] constexpr std::size_t l1d() const noexcept { return m_l1d; }
    [[nodiscard]] constexpr std::size_t l2() const noexcept { return m_l2; }    ///< a core's own (shared by its hardware threads)
    [[nodiscard]] constexpr std::size_t l3() const noexcept { return m_l3; }    ///< the last level, shared; none reported: the L2 or 8 MB, the bigger
    [[nodiscard]] constexpr std::size_t line() const noexcept { return m_line; }
    [[nodiscard]] constexpr bool operator==(const cache_layout&) const noexcept = default;

private:
    std::size_t m_l1d = typical_l1d;
    std::size_t m_l2 = typical_l2;
    std::size_t m_l3 = typical_l3;
    std::size_t m_line = typical_line;
};
static_assert(cache_layout(32768, 524288, 33554432, 64).l2() == 524288 &&                  // a Ryzen 7 5800X
              cache_layout(0, 0, 0, 0) == cache_layout() && cache_layout(0, 16 << 20, 0, 128).l3() == 16 << 20 &&   // Apple M1: no L3 reported
              cache_layout(platform::reported_caches{}) == cache_layout());

/// This processor: its caches and the vector instructions a kernel may choose at run time.
class cpu {
public:
    constexpr cpu(cache_layout caches, bool avx2) noexcept : m_caches(caches), m_avx2(avx2) {}

    [[nodiscard]] constexpr const cache_layout& caches() const noexcept { return m_caches; }
    /// AVX2 is there and the system saves its registers: a [[gnu::target("avx2")]] kernel may run.
    /// Every x86-64 has SSE2 and every arm64 NEON, which need no asking.
    [[nodiscard]] constexpr bool avx2() const noexcept { return m_avx2; }

private:
    cache_layout m_caches;
    bool m_avx2;
};
static_assert(cpu(cache_layout(), true).avx2() && cpu(cache_layout(), false).caches().line() == 64);

namespace detail {

[[nodiscard]] inline bool system_avx2() noexcept {
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");   // the CPUID bit, and the system saving the YMM registers
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
    int r[4];
    __cpuid(r, 1);
    const bool osxsave = (r[2] & (1 << 27)) != 0, avx = (r[2] & (1 << 28)) != 0;
    if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;   // the system saves XMM and YMM
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}

}  // namespace detail

/// Whether an AVX2 kernel may run here, read on first use apart from the caches: a scan asks it
/// first thing, and probing the caches is sysconf here, a dozen /sys files on arm64.
[[nodiscard]] inline bool has_avx2() noexcept {
    static const bool here = detail::system_avx2();
    return here;
}

/// This machine's processor, read on first use (a function-local static: thread-safe, once).
[[nodiscard]] inline const cpu& this_cpu() noexcept {
    static const cpu here(cache_layout(platform::system_caches()), has_avx2());
    return here;
}

}  // namespace pygim
