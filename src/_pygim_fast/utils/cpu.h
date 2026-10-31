#pragma once
// utils/cpu.h — what this machine's processor offers a hot loop: its caches and its vector width.
//
// CORE layer: pybind-free. Read once, on first use, from the operating system — Linux sysconf with
// /sys/devices/system/cpu as the fallback, macOS sysctl, Windows GetLogicalProcessorInformationEx —
// and on x86-64 from the CPUID instruction. A size the system does not tell is a typical one
// (cache_layout::typical), never zero, so whatever is derived from it stays usable.
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <vector>
#  if defined(_M_X64) || defined(_M_AMD64)
#    include <immintrin.h>
#    include <intrin.h>
#  endif
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#else
#  include <unistd.h>
#  include <cstdio>
#endif

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
              cache_layout(0, 0, 0, 0) == cache_layout() && cache_layout(0, 16 << 20, 0, 128).l3() == 16 << 20);   // Apple M1: no L3 reported

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

// A size or a count as the kernel writes one under /sys: "512K" -> 524288, "64" -> 64, "8M"; 0 for
// anything else.
[[nodiscard]] constexpr std::size_t sys_size(std::string_view text) noexcept {
    std::size_t v = 0, i = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) v = v * 10 + static_cast<std::size_t>(text[i] - '0');
    if (i < text.size() && (text[i] == 'K' || text[i] == 'k')) v *= 1024;
    if (i < text.size() && (text[i] == 'M' || text[i] == 'm')) v *= 1024 * 1024;
    return v;
}
static_assert(sys_size("512K\n") == 524288 && sys_size("64") == 64 && sys_size("8M") == 8 << 20 && sys_size("Data") == 0);

/// One cache the kernel lists for a core (/sys/devices/system/cpu/cpu0/cache/index<i>): its level,
/// whether it holds data — its `type` "Data" or "Unified", not "Instruction" — its size and line.
class listed_cache {
public:
    constexpr listed_cache(std::size_t level, std::string_view type, std::size_t bytes, std::size_t line) noexcept
        : m_level(level), m_data(!type.starts_with("Instruction")), m_bytes(bytes), m_line(line) {}

    [[nodiscard]] constexpr std::size_t level() const noexcept { return m_level; }
    [[nodiscard]] constexpr bool holds_data() const noexcept { return m_data; }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return m_bytes; }
    [[nodiscard]] constexpr std::size_t line() const noexcept { return m_line; }

private:
    std::size_t m_level;
    bool m_data;
    std::size_t m_bytes;
    std::size_t m_line;
};

/// The data caches a core sees in the kernel's list, as {L1d, L2, L3, line}: each level's largest
/// cache that holds data, the L1's line. Found 2026-10-29: the type was read from the size's
/// first byte, so a 64 KB L1 instruction cache stood as the L1d where /sys is the only source.
template <class Caches>
[[nodiscard]] constexpr std::array<std::size_t, 4> data_caches(const Caches& listed) noexcept {
    std::array<std::size_t, 4> out{};   // [1..3] the levels, [0] the line
    for (const listed_cache& c : listed) {
        if (!c.holds_data() || c.level() < 1 || c.level() > 3) continue;
        if (c.bytes() > out[c.level()]) out[c.level()] = c.bytes();
        if (c.level() == 1 && out[0] == 0) out[0] = c.line();
    }
    return out;
}
static_assert([] {   // a Zen 3 core as /sys lists it, and an L1 instruction cache larger than the L1d
    const std::array<listed_cache, 5> zen{listed_cache(1, "Data", 32768, 64), listed_cache(1, "Instruction", 65536, 64), listed_cache(2, "Unified", 524288, 64),
                                          listed_cache(3, "Unified", 33554432, 64), listed_cache(4, "Unified", 1, 64)};
    return data_caches(zen) == std::array<std::size_t, 4>{64, 32768, 524288, 33554432};
}());

#if defined(_WIN32)
[[nodiscard]] inline cache_layout system_caches() noexcept {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &bytes);
    if (bytes == 0) return {};
    std::vector<unsigned char> buf(bytes);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationCache, info, &bytes)) return {};
    std::array<std::size_t, 4> size{};
    std::size_t line = 0;
    for (DWORD at = 0; at < bytes;) {
        const auto* e = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + at);
        const CACHE_RELATIONSHIP& c = e->Cache;
        if (e->Relationship == RelationCache && c.Level >= 1 && c.Level <= 3 && (c.Type == CacheData || c.Type == CacheUnified)) {
            size[c.Level] = c.CacheSize > size[c.Level] ? c.CacheSize : size[c.Level];
            if (c.Level == 1) line = c.LineSize;
        }
        at += e->Size;
    }
    return {size[1], size[2], size[3], line};
}
#elif defined(__APPLE__)
[[nodiscard]] inline std::size_t sysctl_size(const char* name) noexcept {
    std::uint64_t v = 0;
    std::size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
    return len == sizeof(std::uint32_t) ? static_cast<std::size_t>(static_cast<std::uint32_t>(v)) : static_cast<std::size_t>(v);
}
[[nodiscard]] inline cache_layout system_caches() noexcept {
    // Apple silicon reports per performance level; the fast cores' caches are the ones a hot loop gets.
    const std::size_t l1 = sysctl_size("hw.perflevel0.l1dcachesize"), l2 = sysctl_size("hw.perflevel0.l2cachesize");
    return {l1 ? l1 : sysctl_size("hw.l1dcachesize"), l2 ? l2 : sysctl_size("hw.l2cachesize"), sysctl_size("hw.l3cachesize"),
            sysctl_size("hw.cachelinesize")};
}
#else
// What /sys/devices/system/cpu/cpu0/cache/index<i>/<what> says ("512K", "Data"); empty when it is not there.
[[nodiscard]] inline std::string_view sys_cache_text(int index, const char* what, std::array<char, 32>& buffer) noexcept {
    char path[96];
    std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu0/cache/index%d/%s", index, what);
    std::FILE* f = std::fopen(path, "r");
    if (f == nullptr) return {};
    const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), f);
    std::fclose(f);
    return {buffer.data(), got};
}
[[nodiscard]] inline cache_layout system_caches() noexcept {
    std::array<std::size_t, 4> size{};
    std::size_t line = 0;
#if defined(_SC_LEVEL1_DCACHE_SIZE) && defined(_SC_LEVEL2_CACHE_SIZE) && defined(_SC_LEVEL3_CACHE_SIZE)
    auto conf = [](int name) { const long v = ::sysconf(name); return v > 0 ? static_cast<std::size_t>(v) : std::size_t{0}; };
    size = {0, conf(_SC_LEVEL1_DCACHE_SIZE), conf(_SC_LEVEL2_CACHE_SIZE), conf(_SC_LEVEL3_CACHE_SIZE)};
    line = conf(_SC_LEVEL1_DCACHE_LINESIZE);
#endif
    if (size[1] == 0 || size[2] == 0) {   // glibc on arm64 and musl report nothing: the kernel's own list
        std::vector<listed_cache> listed;
        std::array<char, 32> level, type, bytes, coherency;
        for (int i = 0; i < 8; ++i) {
            listed.emplace_back(sys_size(sys_cache_text(i, "level", level)), sys_cache_text(i, "type", type),
                                sys_size(sys_cache_text(i, "size", bytes)), sys_size(sys_cache_text(i, "coherency_line_size", coherency)));
        }
        const std::array<std::size_t, 4> found = data_caches(listed);
        size = {0, found[1], found[2], found[3]};
        if (line == 0) line = found[0];
    }
    return {size[1], size[2], size[3], line};
}
#endif

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
    static const cpu here(detail::system_caches(), has_avx2());
    return here;
}

}  // namespace pygim
