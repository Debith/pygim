#pragma once
// platform/memory.h — the process's memory as the system keeps it: how much of it is resident
// now and at most. Infrastructure behind the platform boundary (docs/design/platform_boundary.md,
// D77): utils/memory.h says what the numbers mean, and never asks which system it runs on.
//
// Every system's probes have one contract: bytes, 0 when the call fails (never a throw — a probe
// inside a measured loop must not change what it measures), and peak >= current.
//
// Cost: Linux reads /proc/self/statm through a descriptor opened once (a pread, no open/close per
// call — about a microsecond, the kernel renders the text); macOS asks the Mach task; Windows asks
// the process's working set.
//
// CORE layer: pybind-free.

#include <cstddef>
#include <cstdint>

#include "windows_headers.h"

#if defined(_WIN32)
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <sys/resource.h>
#else
#  include <fcntl.h>
#  include <sys/resource.h>
#  include <unistd.h>
#  include <cstdlib>
#endif

namespace pygim::platform {

#if defined(_WIN32)

/// GetProcessMemoryInfo, WorkingSetSize — the pages of this process currently in physical memory.
/// One kernel call.
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return static_cast<std::uint64_t>(pmc.WorkingSetSize);
}
/// The same call, PeakWorkingSetSize — the largest working set the process has had. Maintained by
/// the kernel as it goes, so it never trails the current value the way Linux's does.
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
}

#elif defined(__APPLE__)

/// task_info(MACH_TASK_BASIC_INFO) on the calling task, resident_size — the bytes of this process
/// in physical memory. One Mach call.
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return static_cast<std::uint64_t>(info.resident_size);
}
/// getrusage(RUSAGE_SELF) ru_maxrss, which the Darwin kernel reports in BYTES (Linux reports the
/// same field in kilobytes). The high-water mark of the resident size.
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    return static_cast<std::uint64_t>(ru.ru_maxrss);   // bytes on macOS
}

#else

namespace detail {
/// The descriptor on /proc/self/statm, opened once for the life of the process (a function-local
/// static: thread-safe initialisation, no open/close per probe). -1 when procfs is not there — the
/// probes then read 0. O_CLOEXEC so a child exec'd from Python does not inherit it. The file reads
/// as one line, "size resident shared text lib data dt", in pages.
inline int statm_fd() noexcept {
    static const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    return fd;
}
/// The page size statm's counts are in — sysconf(_SC_PAGESIZE), asked once (4096 on x86-64 and
/// most arm64 kernels; 16384 on some arm64 ones).
inline long page_size() noexcept {
    static const long ps = ::sysconf(_SC_PAGESIZE);
    return ps;
}
}  // namespace detail

/// One pread of /proc/self/statm at offset 0 through the once-opened descriptor, the second field
/// (resident pages) times the page size. The kernel renders the line on each read, which is the
/// whole cost — about 1 us measured (test_rss_probe_is_cheap allows 20 us for CI). The line is well
/// under the 127-byte buffer; the first field is parsed only to find where the second begins.
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    const int fd = detail::statm_fd();
    if (fd < 0) return 0;
    char buf[128];
    const ssize_t n = ::pread(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char* end = nullptr;
    (void)std::strtoull(buf, &end, 10);                       // size
    const unsigned long long resident = std::strtoull(end, nullptr, 10);
    return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(detail::page_size());
}
/// getrusage(RUSAGE_SELF) ru_maxrss, which Linux reports in KILOBYTES (hence the * 1024), or the
/// current reading — whichever is larger. The kernel updates ru_maxrss at scheduling events rather
/// than on every page fault, so straight after a burst of allocation it can sit a few pages below
/// what statm shows; returning the larger keeps the promise that peak >= current. Two calls.
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    rusage ru{};
    const std::uint64_t now = resident_bytes();
    if (getrusage(RUSAGE_SELF, &ru) != 0) return now;
    const std::uint64_t sampled = static_cast<std::uint64_t>(ru.ru_maxrss) * 1024u;   // kilobytes on Linux
    return sampled > now ? sampled : now;
}

#endif

}  // namespace pygim::platform
