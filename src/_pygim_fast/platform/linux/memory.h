#pragma once
// platform/linux/memory.h — the process's memory as Linux keeps it, behind platform/memory.h's
// process_memory. Only that header includes it, and only where the system is neither Windows nor
// macOS: nothing here asks which system it runs on (D106).
//
// Cost: resident_bytes() reads /proc/self/statm through a descriptor opened once (a pread, no
// open/close per call — about a microsecond, the kernel renders the text); the peak adds getrusage.
//
// CORE layer: pybind-free.

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>

namespace pygim::platform {

class linux_memory {
public:
    /// One pread of /proc/self/statm at offset 0 through the once-opened descriptor, the second field
    /// (resident pages) times the page size. The kernel renders the line on each read, which is the
    /// whole cost — about 1 us measured (test_rss_probe_is_cheap allows 20 us for CI). The line is well
    /// under the 127-byte buffer; the first field is parsed only to find where the second begins.
    [[nodiscard]] static std::uint64_t resident_bytes() noexcept {
        const int fd = statm_fd();
        if (fd < 0) return 0;
        char buf[128];
        const ssize_t n = ::pread(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return 0;
        buf[n] = '\0';
        char* end = nullptr;
        (void)std::strtoull(buf, &end, 10);                       // size
        const unsigned long long resident = std::strtoull(end, nullptr, 10);
        return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(page_size());
    }

    /// getrusage(RUSAGE_SELF) ru_maxrss, which Linux reports in KILOBYTES (hence the * 1024), or the
    /// current reading — whichever is larger. The kernel updates ru_maxrss at scheduling events rather
    /// than on every page fault, so straight after a burst of allocation it can sit a few pages below
    /// what statm shows; returning the larger keeps the promise that peak >= current. Two calls.
    [[nodiscard]] static std::uint64_t peak_resident_bytes() noexcept {
        rusage ru{};
        const std::uint64_t now = resident_bytes();
        if (getrusage(RUSAGE_SELF, &ru) != 0) return now;
        const std::uint64_t sampled = static_cast<std::uint64_t>(ru.ru_maxrss) * 1024u;   // kilobytes on Linux
        return sampled > now ? sampled : now;
    }

private:
    /// The descriptor on /proc/self/statm, opened once for the life of the process (a function-local
    /// static: thread-safe initialisation, no open/close per probe). -1 when procfs is not there — the
    /// probes then read 0. O_CLOEXEC so a child exec'd from Python does not inherit it. The file reads
    /// as one line, "size resident shared text lib data dt", in pages.
    static int statm_fd() noexcept {
        static const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
        return fd;
    }

    /// The page size statm's counts are in — sysconf(_SC_PAGESIZE), asked once (4096 on x86-64 and
    /// most arm64 kernels; 16384 on some arm64 ones).
    static long page_size() noexcept {
        static const long ps = ::sysconf(_SC_PAGESIZE);
        return ps;
    }
};

}  // namespace pygim::platform
