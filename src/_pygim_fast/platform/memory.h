#pragma once
// platform/memory.h — the process's memory as the system keeps it: how much of it is resident
// now and at most. Infrastructure behind the platform boundary (docs/design/platform_boundary.md,
// D77): utils/memory.h says what the numbers mean, and never asks which system it runs on.
//
// Each system's class is a template on the calls it makes (D138): the logic — which field of the
// statm line is the resident count, that ru_maxrss is kilobytes on Linux and bytes on macOS, that a
// peak never reads below the present, that a failed call reads 0 — lives here, in basic_linux_memory,
// basic_macos_memory and basic_windows_memory, compiled and proven on every system over fake calls
// (tests/static/platform_proofs.cpp); the calls are one thin wrapper each, in a file of the system's
// own that asks nothing (D106): linux/memory.h, macos/memory.h, windows/memory.h. This header picks
// the running system's once:
//
//     platform::native_memory::resident_bytes()        -> bytes in physical memory now
//     platform::native_memory::peak_resident_bytes()   -> the most there has been
//
// Every system's probes have one contract: bytes, 0 when the call fails (never a throw — a probe
// inside a measured loop must not change what it measures), and peak >= current where the system
// lets us promise it.
//
// CORE layer: pybind-free.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace pygim::platform {

/// What every system's memory class answers.
template <class M>
concept process_memory = requires {
    { M::resident_bytes() } noexcept -> std::same_as<std::uint64_t>;
    { M::peak_resident_bytes() } noexcept -> std::same_as<std::uint64_t>;
};

/// Linux's calls: the statm line read into a buffer (0 bytes when procfs is not there), the page
/// size its counts are in, and ru_maxrss as the kernel gives it (nothing when getrusage fails).
template <class C>
concept linux_memory_calls = requires(char* buf, std::size_t n) {
    { C::read_statm(buf, n) } noexcept -> std::same_as<std::size_t>;
    { C::page_size() } noexcept -> std::same_as<std::uint64_t>;
    { C::max_rss() } noexcept -> std::same_as<std::optional<std::uint64_t>>;
};

/// macOS's calls: the Mach task's resident size, and ru_maxrss; nothing when a call fails.
template <class C>
concept macos_memory_calls = requires {
    { C::task_resident_size() } noexcept -> std::same_as<std::optional<std::uint64_t>>;
    { C::max_rss() } noexcept -> std::same_as<std::optional<std::uint64_t>>;
};

/// Windows' call: the working set and its peak together; nothing when the call fails.
template <class C>
concept windows_memory_calls = requires {
    { C::working_set() } noexcept -> std::same_as<std::optional<std::pair<std::uint64_t, std::uint64_t>>>;
};

/// Linux: /proc/self/statm's second field, in pages, times the page size; the peak from ru_maxrss,
/// which Linux reports in KILOBYTES and updates at scheduling events rather than on every page
/// fault, so the present reading wins when it is larger.
template <linux_memory_calls Calls>
class basic_linux_memory {
public:
    [[nodiscard]] static constexpr std::uint64_t resident_bytes() noexcept {
        char buf[128] = {};
        const std::size_t n = Calls::read_statm(buf, sizeof(buf) - 1);
        if (n == 0) return 0;
        return resident_pages({buf, n}) * Calls::page_size();
    }
    [[nodiscard]] static constexpr std::uint64_t peak_resident_bytes() noexcept {
        const std::uint64_t now = resident_bytes();
        const std::optional<std::uint64_t> sampled = Calls::max_rss();
        if (!sampled) return now;
        const std::uint64_t peak = *sampled * 1024u;
        return peak > now ? peak : now;
    }

private:
    /// The second field of "size resident shared text lib data dt".
    [[nodiscard]] static constexpr std::uint64_t resident_pages(std::string_view line) noexcept {
        std::size_t i = 0;
        while (i < line.size() && line[i] != ' ') ++i;
        while (i < line.size() && line[i] == ' ') ++i;
        std::uint64_t pages = 0;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') pages = pages * 10 + static_cast<std::uint64_t>(line[i++] - '0');
        return pages;
    }
};

/// macOS: the task's resident size in bytes; the peak from ru_maxrss, which Darwin reports in BYTES.
template <macos_memory_calls Calls>
class basic_macos_memory {
public:
    [[nodiscard]] static constexpr std::uint64_t resident_bytes() noexcept { return Calls::task_resident_size().value_or(0); }
    [[nodiscard]] static constexpr std::uint64_t peak_resident_bytes() noexcept { return Calls::max_rss().value_or(0); }
};

/// Windows: the working set and its peak, which the kernel maintains as it goes.
template <windows_memory_calls Calls>
class basic_windows_memory {
public:
    [[nodiscard]] static constexpr std::uint64_t resident_bytes() noexcept {
        const auto set = Calls::working_set();
        return set ? set->first : 0;
    }
    [[nodiscard]] static constexpr std::uint64_t peak_resident_bytes() noexcept {
        const auto set = Calls::working_set();
        return set ? set->second : 0;
    }
};

}  // namespace pygim::platform

// The one choice: each system's calls file is included only where its headers exist.
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
