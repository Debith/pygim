#pragma once
// platform/clocks.h — the calling thread's CPU time, where the system counts it. Infrastructure
// behind the platform boundary (docs/design/platform_boundary.md, D77): utils/timestamp.h's
// thread_cpu_clock reads it, and falls back to its measuring clock where counts_thread_cpu_time
// is false. The wall and measuring clocks are std::chrono's, the same everywhere.
//
// CORE layer: pybind-free.

#include <cstdint>

#ifndef _WIN32
#include <ctime>
#endif

#include "native.h"

namespace pygim::platform {

/// Whether thread_cpu_ns() counts: POSIX's CLOCK_THREAD_CPUTIME_ID. Not on Windows here (its
/// GetThreadTimes counts in scheduler ticks, about 15 ms).
inline constexpr bool counts_thread_cpu_time = native != family::windows;

/// Nanoseconds the calling thread has run, its waiting left out; 0 where counts_thread_cpu_time
/// is false. About 500 ns a read on Linux.
[[nodiscard]] inline std::int64_t thread_cpu_ns() noexcept {
#ifdef _WIN32
    return 0;
#else
    timespec ts{};
    ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
#endif
}

}  // namespace pygim::platform
