#pragma once
// utils/memory.h — the process's resident memory, as one syscall.
//
// CORE layer: pybind-free. resident_bytes() is what the OS holds in physical
// memory for this process right now; peak_resident_bytes() the high-water
// mark. Both are process-wide numbers (every object, the interpreter, memory
// the allocator keeps after a free): the right probe for a benchmark's
// before/after delta on a fresh heap, never a per-object size — components
// report their own exact bytes() for that (docs/design/mapping_toolkit.md).
//
// Cost: Linux reads /proc/self/statm through a file descriptor opened once
// (a pread, no open/close per call — about a microsecond, the kernel renders
// the text); macOS asks the Mach task; Windows asks the process's working
// set. Cheap enough to sit inside a measured loop without moving the number.
//
// Linux's peak (getrusage ru_maxrss) is sampled by the kernel at scheduling
// events and can trail the current value by a few pages, so the peak reported
// here is never below the present reading.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
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

namespace pygim::memory {

#if defined(_WIN32)
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return static_cast<std::uint64_t>(pmc.WorkingSetSize);
}
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
}
#elif defined(__APPLE__)
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return static_cast<std::uint64_t>(info.resident_size);
}
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    return static_cast<std::uint64_t>(ru.ru_maxrss);   // bytes on macOS
}
#else
namespace detail {
// /proc/self/statm: "size resident shared text lib data dt" in pages.
inline int statm_fd() noexcept {
    static const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    return fd;
}
inline long page_size() noexcept {
    static const long ps = ::sysconf(_SC_PAGESIZE);
    return ps;
}
}  // namespace detail
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
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    rusage ru{};
    const std::uint64_t now = resident_bytes();
    if (getrusage(RUSAGE_SELF, &ru) != 0) return now;
    const std::uint64_t sampled = static_cast<std::uint64_t>(ru.ru_maxrss) * 1024u;   // kilobytes on Linux
    return sampled > now ? sampled : now;
}
#endif

[[nodiscard]] inline double resident_mb() noexcept { return static_cast<double>(resident_bytes()) / (1024.0 * 1024.0); }
[[nodiscard]] inline double peak_resident_mb() noexcept { return static_cast<double>(peak_resident_bytes()) / (1024.0 * 1024.0); }

}  // namespace pygim::memory
