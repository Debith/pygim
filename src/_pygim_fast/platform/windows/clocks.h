#pragma once
// platform/windows/clocks.h — the calling thread's CPU time on Windows, behind platform/clocks.h's
// thread_clocks. Only that header includes it, and only on Windows: nothing here asks which system
// it runs on (D106).
//
// Windows counts a thread's CPU time in scheduler ticks (GetThreadTimes, about 15 ms), too coarse for
// one task's share, so this class reads the measuring clock instead: a thread here looks as if it
// never waits, and a pool that sizes itself by the waiting it sees stays at the cores.
//
// CORE layer: pybind-free.

#include <chrono>
#include <cstdint>

namespace pygim::platform {

class windows_clocks {
public:
    /// The measuring clock's nanoseconds, standing in for the thread's CPU time (see above).
    [[nodiscard]] static std::int64_t thread_cpu_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

}  // namespace pygim::platform
