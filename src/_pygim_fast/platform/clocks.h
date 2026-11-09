#pragma once
// platform/clocks.h — the calling thread's CPU time, as each system counts it. Infrastructure
// behind the platform boundary (docs/design/platform_boundary.md, D77):
// core/primitives/timestamp.h's thread_cpu_clock reads it, and never asks which system it runs on.
// The wall and measuring clocks are std::chrono's, the same everywhere, and read in timestamp.h
// itself.
//
// One class per system, each in a file of its own that asks nothing (D106): posix/clocks.h and
// windows/clocks.h. This header states what every one of them answers and picks the running
// system's, once:
//
//     platform::native_clocks::thread_cpu_ns()   -> nanoseconds the calling thread has run
//
// Neither class puts logic above its call, so neither is a template on its calls (D138): a fake would
// prove only itself. They are their calls, proven by CI on their own systems.
//
// CORE layer: pybind-free.

#include <concepts>
#include <cstdint>

namespace pygim::platform {

/// What every system's clocks class answers: the calling thread's time on a CPU in nanoseconds,
/// counted from the thread's start, so only a difference read on one thread means anything.
template <class C>
concept thread_clocks = requires {
    { C::thread_cpu_ns() } noexcept -> std::same_as<std::int64_t>;
};

}  // namespace pygim::platform

// The one choice: each system's file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/clocks.h"
namespace pygim::platform { using native_clocks = windows_clocks; }
#else
#include "posix/clocks.h"
namespace pygim::platform { using native_clocks = posix_clocks; }
#endif

static_assert(pygim::platform::thread_clocks<pygim::platform::native_clocks>);
