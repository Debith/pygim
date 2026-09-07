#pragma once
// utils/memory.h — the process's resident memory, as one syscall.
//
// CORE layer: pybind-free. resident_bytes() is what the OS holds in physical
// memory for this process right now; peak_resident_bytes() the high-water
// mark. Both are process-wide numbers (every object, the interpreter, memory
// the allocator keeps after a free): the right probe for a benchmark's
// before/after delta on a fresh heap, never a per-object size — components
// report their own exact bytes() for that (docs/design/mapping_toolkit.md).
// Bound to Python as utils.rss_bytes / peak_rss_bytes / rss_mb / peak_rss_mb
// (utils/bindings.cpp).
//
// Cost: Linux reads /proc/self/statm through a file descriptor opened once
// (a pread, no open/close per call — about a microsecond, the kernel renders
// the text); macOS asks the Mach task; Windows asks the process's working
// set. Cheap enough to sit inside a measured loop without moving the number.
//
// Linux's peak (getrusage ru_maxrss) is sampled by the kernel at scheduling
// events and can trail the current value by a few pages, so the peak reported
// here is never below the present reading.
//
// A worked example, used in the comments below — what
// tests/unittests/test_utils.py::test_rss_grows_with_a_large_allocation does:
//
//     before = rss_bytes()
//     block  = bytearray(64 * 2**20)          # 64 MiB reserved, not yet resident
//     block[::4096] = b"x" * ...              # one byte per page: now it is
//     after  = rss_bytes()
//
//     after - before        -> about 64 MiB (16384 pages of 4 KiB; the test accepts > 32 MiB)
//     peak_rss_bytes()      -> >= after
//     del block; rss_bytes()-> not necessarily back to `before`
//
// Only the delta means anything. The absolute reading is the whole process;
// and once `block` is freed the allocator may keep the pages, so a second
// measurement of the same allocation in the same process can show no growth
// at all — a benchmark takes its delta on a fresh heap, once.

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

// ── the probes, one pair per platform ─────────────────────────────────────
// Every branch defines the same two functions with the same contract: bytes,
// 0 when the OS call fails (never a throw — a probe inside a measured loop
// must not change what it measures), and peak >= current.

#if defined(_WIN32)
/// Windows: GetProcessMemoryInfo, WorkingSetSize — the pages of this process
/// currently in physical memory. One kernel call.
///
///     after - before   // about 64 MiB for the worked example
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return static_cast<std::uint64_t>(pmc.WorkingSetSize);
}
/// Windows: the same call, PeakWorkingSetSize — the largest working set the
/// process has had. Maintained by the kernel as it goes, so it never trails
/// the current value the way Linux's does.
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
}
#elif defined(__APPLE__)
/// macOS: task_info(MACH_TASK_BASIC_INFO) on the calling task, resident_size
/// — the bytes of this process in physical memory. One Mach call.
///
///     after - before   // about 64 MiB for the worked example
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept {
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return static_cast<std::uint64_t>(info.resident_size);
}
/// macOS: getrusage(RUSAGE_SELF) ru_maxrss, which the Darwin kernel reports
/// in BYTES (Linux reports the same field in kilobytes — see the Linux
/// branch). The high-water mark of the resident size.
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    return static_cast<std::uint64_t>(ru.ru_maxrss);   // bytes on macOS
}
#else
namespace detail {
/// The descriptor on /proc/self/statm, opened once for the life of the
/// process (a function-local static: thread-safe initialisation, no
/// open/close per probe). -1 when procfs is not there — the probes then read
/// 0. O_CLOEXEC so a child exec'd from Python does not inherit it. The file
/// reads as one line, "size resident shared text lib data dt", in pages.
inline int statm_fd() noexcept {
    static const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    return fd;
}
/// The page size statm's counts are in — sysconf(_SC_PAGESIZE), asked once
/// (4096 on x86-64 and most arm64 kernels; 16384 on some arm64 ones).
inline long page_size() noexcept {
    static const long ps = ::sysconf(_SC_PAGESIZE);
    return ps;
}
}  // namespace detail
/// Linux: one pread of /proc/self/statm at offset 0 through the once-opened
/// descriptor, the second field (resident pages) times the page size. The
/// kernel renders the line on each read, which is the whole cost — about
/// 1 us measured (test_rss_probe_is_cheap allows 20 us for CI).
///
///     after - before   // about 64 MiB: 16384 pages * 4096 in the worked example
///
/// The line is well under the 127-byte buffer; the first field is parsed
/// only to find where the second begins.
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
/// Linux: getrusage(RUSAGE_SELF) ru_maxrss, which Linux reports in KILOBYTES
/// (hence the * 1024), or the current reading — whichever is larger. The
/// kernel updates ru_maxrss at scheduling events rather than on every page
/// fault, so straight after a burst of allocation it can sit a few pages
/// below what statm shows; returning the larger keeps the promise that
/// peak >= current, which test_rss_probes_are_positive_and_consistent and
/// the worked example (peak_rss_bytes() >= after) rely on. Two calls.
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept {
    rusage ru{};
    const std::uint64_t now = resident_bytes();
    if (getrusage(RUSAGE_SELF, &ru) != 0) return now;
    const std::uint64_t sampled = static_cast<std::uint64_t>(ru.ru_maxrss) * 1024u;   // kilobytes on Linux
    return sampled > now ? sampled : now;
}
#endif

// ── in mebibytes ──────────────────────────────────────────────────────────

/// resident_bytes() / 2^20, as a double — for a printed figure or a
/// benchmark's JSONL row, not for arithmetic.        after - before -> ~64.0
[[nodiscard]] inline double resident_mb() noexcept { return static_cast<double>(resident_bytes()) / (1024.0 * 1024.0); }
/// peak_resident_bytes() / 2^20, as a double.        >= resident_mb() at the same moment
[[nodiscard]] inline double peak_resident_mb() noexcept { return static_cast<double>(peak_resident_bytes()) / (1024.0 * 1024.0); }

}  // namespace pygim::memory
