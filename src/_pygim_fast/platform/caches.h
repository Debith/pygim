#pragma once
// platform/caches.h — the data caches one core sees, as the system reports them. Infrastructure
// behind the platform boundary (docs/design/platform_boundary.md): core/primitives/cpu.h makes a
// cache_layout of what this reports, and never asks which system it runs on.
//
// One class per system, each in a file of its own that asks nothing: linux/caches.h,
// macos/caches.h, windows/caches.h. This header states what every one of them answers and picks the
// running system's, once:
//
//     platform::native_caches::system_caches()   -> {32768, 524288, 33554432, 64} on a Ryzen 7 5800X
//
// Every system's reading has one contract: bytes, and 0 for a size the system does not report
// (never a throw: cpu.h then takes a typical size).
//
// Only Linux puts logic above its calls: sysconf first, and where it reports no L1d or L2 (glibc on
// arm64, musl) the kernel's own list under /sys/devices/system/cpu/cpu0/cache, whose sizes and types
// are read by rules here (sys_size, listed_cache, data_caches). So only Linux's class is a template
// on its calls, basic_linux_caches, proven over fake calls on every system
// (tests/static/platform_proofs.cpp). macOS's and Windows' classes hand the system's reading on as
// it is, so a fake would prove only itself: they are their calls, proven by CI on their own systems.
//
// CORE layer: pybind-free.

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <vector>

namespace pygim::platform {

/// The data caches one core sees, in bytes, as a system says: its L1d and L2, the L3 its cores
/// share, and the L1's line. 0 for what the system does not say.
class reported_caches {
public:
    constexpr reported_caches() noexcept = default;
    constexpr reported_caches(std::size_t l1d, std::size_t l2, std::size_t l3, std::size_t line) noexcept
        : m_l1d(l1d), m_l2(l2), m_l3(l3), m_line(line) {}

    [[nodiscard]] constexpr std::size_t l1d() const noexcept { return m_l1d; }
    [[nodiscard]] constexpr std::size_t l2() const noexcept { return m_l2; }
    [[nodiscard]] constexpr std::size_t l3() const noexcept { return m_l3; }
    [[nodiscard]] constexpr std::size_t line() const noexcept { return m_line; }
    /// Whether the system said what a core's own caches are: an L1d and an L2.
    [[nodiscard]] constexpr bool knows_a_cores_caches() const noexcept { return m_l1d != 0 && m_l2 != 0; }
    [[nodiscard]] constexpr bool operator==(const reported_caches&) const noexcept = default;

private:
    std::size_t m_l1d = 0;
    std::size_t m_l2 = 0;
    std::size_t m_l3 = 0;
    std::size_t m_line = 0;
};
static_assert(reported_caches(32768, 524288, 33554432, 64).knows_a_cores_caches() && !reported_caches().knows_a_cores_caches());

/// What every system's caches class answers.
template <class C>
concept cache_reports = requires {
    { C::system_caches() } noexcept -> std::same_as<reported_caches>;
};

/// A size or a count as the kernel writes one under /sys: "512K" -> 524288, "64" -> 64, "8M"; 0 for
/// anything else.
[[nodiscard]] constexpr std::size_t sys_size(std::string_view text) noexcept {
    std::size_t v = 0, i = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) v = v * 10 + static_cast<std::size_t>(text[i] - '0');
    if (i < text.size() && (text[i] == 'K' || text[i] == 'k')) v *= 1024;
    if (i < text.size() && (text[i] == 'M' || text[i] == 'm')) v *= 1024 * 1024;
    return v;
}
static_assert(sys_size("512K\n") == 524288 && sys_size("64") == 64 && sys_size("8M") == 8 << 20 && sys_size("Data") == 0);

/// One cache the kernel lists for a core (/sys/devices/system/cpu/cpu0/cache/index<i>): its level,
/// whether it holds data (its `type` "Data" or "Unified", not "Instruction"), its size and line.
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
static_assert(listed_cache(1, "Data\n", 32768, 64).holds_data() && !listed_cache(1, "Instruction\n", 65536, 64).holds_data());

/// The data caches a core sees in the kernel's list: each level's largest cache that holds data,
/// and the L1's line. Found 2026-10-29: the type was read from the size's first byte, so a 64 KB L1
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

/// Linux: sysconf's caches, and where it reports no L1d or L2 (glibc on arm64, musl) the kernel's
/// list of cpu0's caches, the line sysconf reported kept. `Calls` answers sysconf_caches() and
/// sys_cache_text(index, file, buffer): the text of /sys/devices/system/cpu/cpu0/cache/index<i>/<file>,
/// empty when it is not there.
template <class Calls>
struct basic_linux_caches {
    /// The kernel lists at most this many caches for a core: L1d, L1i, L2, L3, and room to spare.
    static constexpr int listed_at_most = 8;

    [[nodiscard]] static constexpr reported_caches system_caches() noexcept {
        const reported_caches conf = Calls::sysconf_caches();
        if (conf.knows_a_cores_caches()) return conf;
        const reported_caches listed = data_caches(kernels_list());
        return {listed.l1d(), listed.l2(), listed.l3(), conf.line() ? conf.line() : listed.line()};
    }

private:
    [[nodiscard]] static constexpr std::vector<listed_cache> kernels_list() noexcept {
        std::vector<listed_cache> listed;
        std::array<char, 32> level{}, type{}, bytes{}, line{};
        for (int i = 0; i < listed_at_most; ++i) {
            listed.emplace_back(sys_size(Calls::sys_cache_text(i, "level", level)), Calls::sys_cache_text(i, "type", type),
                                sys_size(Calls::sys_cache_text(i, "size", bytes)), sys_size(Calls::sys_cache_text(i, "coherency_line_size", line)));
        }
        return listed;
    }
};

}  // namespace pygim::platform

// The one choice: each system's file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/caches.h"
namespace pygim::platform { using native_caches = windows_caches; }
#elif defined(__APPLE__)
#include "macos/caches.h"
namespace pygim::platform { using native_caches = macos_caches; }
#else
#include "linux/caches.h"   // and any other POSIX system: sysconf and /sys where they answer, 0 where not
namespace pygim::platform { using native_caches = linux_caches; }
#endif

static_assert(pygim::platform::cache_reports<pygim::platform::native_caches>);
