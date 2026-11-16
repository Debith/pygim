#pragma once
// platform/posix/clocks.h — the calling thread's CPU time on POSIX, behind platform/clocks.h's
// thread_clocks. Only that header includes it, and only where the system is not Windows: nothing
// here asks which system it runs on (D106).
//
// Cost: one clock_gettime, about 500 ns on Linux: sample it, do not read it per item.
//
// CORE layer: pybind-free.

#include <ctime>

#include <cstdint>

namespace pygim::platform {

class posix_clocks {
public:
    /// clock_gettime(CLOCK_THREAD_CPUTIME_ID): nanoseconds the calling thread has run, its waiting
    /// left out. 0 when the call fails.
    [[nodiscard]] static std::int64_t thread_cpu_ns() noexcept {
        timespec ts{};
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
        return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
    }
};

}  // namespace pygim::platform
