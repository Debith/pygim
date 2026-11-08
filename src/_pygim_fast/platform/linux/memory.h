#pragma once
// platform/linux/memory.h — the calls Linux's memory probes make, one thin wrapper each: a pread of
// /proc/self/statm, the page size, getrusage. The logic over them is basic_linux_memory's, in
// platform/memory.h, proven over fake calls on every system (D138). Only that header includes this,
// and only where the system is neither Windows nor macOS: nothing here asks which system it runs on
// (D106).
//
// Cost: resident_bytes() is one pread through a descriptor opened once (no open/close per call —
// about a microsecond, the kernel renders the text); the peak adds getrusage.
//
// CORE layer: pybind-free.

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

namespace pygim::platform {

struct linux_proc_calls {
    /// The statm line read at offset 0 through the descriptor opened once for the process's life (a
    /// function-local static, O_CLOEXEC so a child exec'd from Python does not inherit it); 0 bytes
    /// when procfs is not there or the read fails.
    [[nodiscard]] static std::size_t read_statm(char* buf, std::size_t n) noexcept {
        static const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
        const ssize_t got = fd < 0 ? 0 : ::pread(fd, buf, n, 0);
        return got <= 0 ? 0 : static_cast<std::size_t>(got);
    }
    /// sysconf(_SC_PAGESIZE), asked once: 4096 on x86-64 and most arm64 kernels, 16384 on some arm64 ones.
    [[nodiscard]] static std::uint64_t page_size() noexcept {
        static const long ps = ::sysconf(_SC_PAGESIZE);
        return static_cast<std::uint64_t>(ps);
    }
    /// getrusage(RUSAGE_SELF) ru_maxrss, kilobytes on Linux; 0 when the call fails.
    [[nodiscard]] static std::uint64_t max_rss_kb() noexcept {
        rusage ru{};
        return ::getrusage(RUSAGE_SELF, &ru) == 0 ? static_cast<std::uint64_t>(ru.ru_maxrss) : 0;
    }
};

using linux_memory = basic_linux_memory<linux_proc_calls>;

}  // namespace pygim::platform
