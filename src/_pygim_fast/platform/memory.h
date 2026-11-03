#pragma once
// platform/memory.h — the process's memory as the system keeps it: how much of it is resident
// now and at most. Infrastructure behind the platform boundary (docs/design/platform_boundary.md,
// D77): utils/memory.h says what the numbers mean, and never asks which system it runs on.
//
// One class per system, each in a file of its own that asks nothing (D106): linux/memory.h,
// macos/memory.h, windows/memory.h. This header states what every one of them answers and picks
// the running system's, once:
//
//     platform::native_memory::resident_bytes()        -> bytes in physical memory now
//     platform::native_memory::peak_resident_bytes()   -> the most there has been
//
// Every system's probes have one contract: bytes, 0 when the call fails (never a throw — a probe
// inside a measured loop must not change what it measures), and peak >= current.
//
// CORE layer: pybind-free.

#include <concepts>
#include <cstdint>

namespace pygim::platform {

/// What every system's memory class answers.
template <class M>
concept process_memory = requires {
    { M::resident_bytes() } noexcept -> std::same_as<std::uint64_t>;
    { M::peak_resident_bytes() } noexcept -> std::same_as<std::uint64_t>;
};

}  // namespace pygim::platform

// The one choice: each system's file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/memory.h"
namespace pygim::platform { using native_memory = windows_memory; }
#elif defined(__APPLE__)
#include "macos/memory.h"
namespace pygim::platform { using native_memory = macos_memory; }
#else
#include "linux/memory.h"   // and any other POSIX system: procfs where it is mounted, 0 where not
namespace pygim::platform { using native_memory = linux_memory; }
#endif

static_assert(pygim::platform::process_memory<pygim::platform::native_memory>);
