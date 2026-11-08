#pragma once
// platform/memory.h — the process's memory as the system keeps it: how much of it is resident
// now and at most. Infrastructure behind the platform boundary (docs/design/platform_boundary.md,
// D77): utils/memory.h says what the numbers mean, and never asks which system it runs on.
//
// One class per system, each in a file of its own that asks nothing (D106): linux/memory.h,
// macos/memory.h, windows/memory.h. This header states what every one of them answers and picks
// the running system's, once:
//
//     platform::native_memory::resident_bytes()        -> bytes in physical memory now
//     platform::native_memory::peak_resident_bytes()   -> the most there has been
//
// Every system's probes have one contract: bytes, 0 when the call fails (never a throw — a probe
// inside a measured loop must not change what it measures), and peak >= current.
//
// Only Linux puts logic above its calls — statm's second field times the page size, a peak in
// kilobytes, the larger of peak and present — so only Linux's class is a template on its calls
// (D138), basic_linux_memory here, proven over fake calls on every system
// (tests/static/platform_proofs.cpp). macOS's and Windows' classes hand the system's reading on as it
// is, so a fake would prove only itself: they are their calls, proven by CI on their own systems.
//
// CORE layer: pybind-free.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pygim::platform {

/// What every system's memory class answers.
template <class M>
concept process_memory = requires {
    { M::resident_bytes() } noexcept -> std::same_as<std::uint64_t>;
    { M::peak_resident_bytes() } noexcept -> std::same_as<std::uint64_t>;
};

/// Linux: /proc/self/statm's second field, in pages, times the page size; the peak from ru_maxrss,
/// which Linux reports in KILOBYTES and updates at scheduling events rather than on every page fault,
/// so the present reading wins when it is larger. A failed call reads 0 (no statm line, no peak).
template <class Calls>
struct basic_linux_memory {
    [[nodiscard]] static constexpr std::uint64_t resident_bytes() noexcept {
        char buf[128] = {};
        const std::size_t n = Calls::read_statm(buf, sizeof(buf));
        return second_field({buf, n}) * Calls::page_size();
    }
    [[nodiscard]] static constexpr std::uint64_t peak_resident_bytes() noexcept {
        const std::uint64_t now = resident_bytes();
        const std::uint64_t peak = Calls::max_rss_kb() * 1024u;
        return peak > now ? peak : now;
    }

private:
    /// The second number of "size resident shared text lib data dt"; 0 for an empty line.
    [[nodiscard]] static constexpr std::uint64_t second_field(std::string_view line) noexcept {
        std::size_t i = line.find(' ');
        std::uint64_t pages = 0;
        for (i = i == std::string_view::npos ? line.size() : i + 1; i < line.size() && line[i] >= '0' && line[i] <= '9'; ++i) {
            pages = pages * 10 + static_cast<std::uint64_t>(line[i] - '0');
        }
        return pages;
    }
};

}  // namespace pygim::platform

// The one choice: each system's file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/memory.h"
namespace pygim::platform { using native_memory = windows_memory; }
#elif defined(__APPLE__)
#include "macos/memory.h"
namespace pygim::platform { using native_memory = macos_memory; }
#else
#include "linux/memory.h"   // and any other POSIX system: procfs where it is mounted, 0 where not
namespace pygim::platform { using native_memory = linux_memory; }
#endif

static_assert(pygim::platform::process_memory<pygim::platform::native_memory>);
