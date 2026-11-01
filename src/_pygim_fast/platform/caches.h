#pragma once
// platform/caches.h — the data caches as the system reports them: Linux's sysconf, with the
// kernel's list under /sys/devices/system/cpu as the fallback; macOS's sysctl; Windows'
// GetLogicalProcessorInformationEx. Infrastructure behind the platform boundary
// (docs/design/platform_boundary.md, D77): utils/cpu.h makes a cache_layout of what this reports,
// and never asks which system it runs on.
//
//     const reported_caches r = system_caches();   // {32768, 524288, 33554432, 64} on a Ryzen 7 5800X
//
// The kernel's list is read by rules compiled everywhere (sys_size, listed_cache, data_caches), so
// they are proven on every system; only the reading of its files is Linux's.
//
// CORE layer: pybind-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "windows_headers.h"

#if defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#elif !defined(_WIN32)
#  include <unistd.h>
#  include <cstdio>
#endif

namespace pygim::platform {

/// The data caches one core sees, in bytes, as the system says: its L1 and L2, the L3 its cores
/// share, the L1's line. 0 where the system does not say.
struct reported_caches {
    std::size_t l1d = 0, l2 = 0, l3 = 0, line = 0;
    [[nodiscard]] constexpr bool operator==(const reported_caches&) const noexcept = default;
};

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

/// The data caches a core sees in the kernel's list: each level's largest cache that holds data,
/// the L1's line. Found 2026-10-29: the type was read from the size's first byte, so a 64 KB L1
/// instruction cache stood as the L1d where /sys is the only source.
template <class Caches>
[[nodiscard]] constexpr reported_caches data_caches(const Caches& listed) noexcept {
    std::array<std::size_t, 4> size{};   // [1..3] the levels
    std::size_t line = 0;
    for (const listed_cache& c : listed) {
        if (!c.holds_data() || c.level() < 1 || c.level() > 3) continue;
        if (c.bytes() > size[c.level()]) size[c.level()] = c.bytes();
        if (c.level() == 1 && line == 0) line = c.line();
    }
    return {size[1], size[2], size[3], line};
}
static_assert([] {   // a Zen 3 core as /sys lists it, and an L1 instruction cache larger than the L1d
    const std::array<listed_cache, 5> zen{listed_cache(1, "Data", 32768, 64), listed_cache(1, "Instruction", 65536, 64), listed_cache(2, "Unified", 524288, 64),
                                          listed_cache(3, "Unified", 33554432, 64), listed_cache(4, "Unified", 1, 64)};
    return data_caches(zen) == reported_caches{32768, 524288, 33554432, 64};
}());

#if defined(_WIN32)

[[nodiscard]] inline reported_caches system_caches() noexcept {
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

namespace detail {
[[nodiscard]] inline std::size_t sysctl_size(const char* name) noexcept {
    std::uint64_t v = 0;
    std::size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
    return len == sizeof(std::uint32_t) ? static_cast<std::size_t>(static_cast<std::uint32_t>(v)) : static_cast<std::size_t>(v);
}
}  // namespace detail

[[nodiscard]] inline reported_caches system_caches() noexcept {
    using detail::sysctl_size;
    // Apple silicon reports per performance level; the fast cores' caches are the ones a hot loop gets.
    const std::size_t l1 = sysctl_size("hw.perflevel0.l1dcachesize"), l2 = sysctl_size("hw.perflevel0.l2cachesize");
    return {l1 ? l1 : sysctl_size("hw.l1dcachesize"), l2 ? l2 : sysctl_size("hw.l2cachesize"), sysctl_size("hw.l3cachesize"),
            sysctl_size("hw.cachelinesize")};
}

#else

namespace detail {
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
}  // namespace detail

[[nodiscard]] inline reported_caches system_caches() noexcept {
    reported_caches r;
#if defined(_SC_LEVEL1_DCACHE_SIZE) && defined(_SC_LEVEL2_CACHE_SIZE) && defined(_SC_LEVEL3_CACHE_SIZE)
    auto conf = [](int name) { const long v = ::sysconf(name); return v > 0 ? static_cast<std::size_t>(v) : std::size_t{0}; };
    r = {conf(_SC_LEVEL1_DCACHE_SIZE), conf(_SC_LEVEL2_CACHE_SIZE), conf(_SC_LEVEL3_CACHE_SIZE), conf(_SC_LEVEL1_DCACHE_LINESIZE)};
#endif
    if (r.l1d == 0 || r.l2 == 0) {   // glibc on arm64 and musl report nothing: the kernel's own list
        using detail::sys_cache_text;
        std::vector<listed_cache> listed;
        std::array<char, 32> level, type, bytes, coherency;
        for (int i = 0; i < 8; ++i) {
            listed.emplace_back(sys_size(sys_cache_text(i, "level", level)), sys_cache_text(i, "type", type),
                                sys_size(sys_cache_text(i, "size", bytes)), sys_size(sys_cache_text(i, "coherency_line_size", coherency)));
        }
        const reported_caches found = data_caches(listed);
        r = {found.l1d, found.l2, found.l3, r.line ? r.line : found.line};
    }
    return r;
}

#endif

}  // namespace pygim::platform
