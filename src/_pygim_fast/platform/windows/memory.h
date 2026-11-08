#pragma once
// platform/windows/memory.h — the call Windows' memory probes make, one thin wrapper, behind
// platform/memory.h's windows_memory_calls: GetProcessMemoryInfo. No logic lives here — that is
// basic_windows_memory's, proven over fake calls on every system (D138). Only platform/memory.h
// includes this, and only on Windows: nothing here asks which system it runs on (D106).
//
// Cost: one kernel call.
//
// CORE layer: pybind-free.

#include <cstdint>
#include <optional>
#include <utility>

#include "../windows_headers.h"

#include <psapi.h>

namespace pygim::platform {

struct win32_memory_calls {
    /// WorkingSetSize and PeakWorkingSetSize, the pages of this process in physical memory now and
    /// at most; nothing when the call fails.
    [[nodiscard]] static std::optional<std::pair<std::uint64_t, std::uint64_t>> working_set() noexcept {
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return std::nullopt;
        return std::pair<std::uint64_t, std::uint64_t>{static_cast<std::uint64_t>(pmc.WorkingSetSize), static_cast<std::uint64_t>(pmc.PeakWorkingSetSize)};
    }
};

using windows_memory = basic_windows_memory<win32_memory_calls>;

}  // namespace pygim::platform
