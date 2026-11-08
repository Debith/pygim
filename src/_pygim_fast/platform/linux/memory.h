#pragma once
// platform/linux/memory.h — the calls Linux's memory probes make, one thin wrapper each, behind
// platform/memory.h's linux_memory_calls: a pread of /proc/self/statm, the page size, getrusage.
// No logic lives here — that is basic_linux_memory's, proven over fake calls on every system
// (D138). Only platform/memory.h includes this, and only where the system is neither Windows nor
// macOS: nothing here asks which system it runs on (D106).
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
#include <optional>

namespace pygim::platform {

struct linux_proc_calls {
    /// The statm line, "size resident shared text lib data dt" in pages, read at offset 0 through
    /// the once-opened descriptor; 0 bytes when procfs is not there or the read fails.
    [[nodiscard]] static std::size_t read_statm(char* buf, std::size_t n) noexcept {
        const int fd = statm_fd();
        if (fd < 0) return 0;
        const ssize_t got = ::pread(fd, buf, n, 0);
        return got <= 0 ? 0 : static_cast<std::size_t>(got);
    }
    /// sysconf(_SC_PAGESIZE), asked once: 4096 on x86-64 and most arm64 kernels, 16384 on some arm64 ones.
    [[nodiscard]] static std::uint64_t page_size() noexcept {
        static const long ps = ::sysconf(_SC_PAGESIZE);
        return ps > 0 ? static_cast<std::uint64_t>(ps) : 4096u;
    }
    /// getrusage(RUSAGE_SELF) ru_maxrss as the kernel gives it (kilobytes here); nothing when it fails.
    [[nodiscard]] static std::optional<std::uint64_t> max_rss() noexcept {
        rusage ru{};
        if (::getrusage(RUSAGE_SELF, &ru) != 0) return std::nullopt;
        return static_cast<std::uint64_t>(ru.ru_maxrss);
    }

private:
    /// The descriptor on /proc/self/statm, opened once for the life of the process (a function-local
    /// static: thread-safe initialisation, no open/close per probe). -1 when procfs is not there.
    /// O_CLOEXEC so a child exec'd from Python does not inherit it.
    [[nodiscard]] static int statm_fd() noexcept {
        static const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
        return fd;
    }
};

using linux_memory = basic_linux_memory<linux_proc_calls>;

}  // namespace pygim::platform
