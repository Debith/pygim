#pragma once
// platform/windows/memory.h — the process's memory as Windows keeps it, behind platform/memory.h's
// process_memory. Only that header includes it, and only on Windows: nothing here asks which system
// it runs on (D106).
//
// Cost: one kernel call each (GetProcessMemoryInfo).
//
// CORE layer: pybind-free.

#include <cstdint>

#include "../windows_headers.h"

#include <psapi.h>

namespace pygim::platform {

class windows_memory {
public:
    /// WorkingSetSize — the pages of this process currently in physical memory.
    [[nodiscard]] static std::uint64_t resident_bytes() noexcept {
        return static_cast<std::uint64_t>(counters().WorkingSetSize);
    }

    /// PeakWorkingSetSize — the largest working set the process has had. Maintained by the kernel as
    /// it goes, so it never trails the current value the way Linux's does.
    [[nodiscard]] static std::uint64_t peak_resident_bytes() noexcept {
        return static_cast<std::uint64_t>(counters().PeakWorkingSetSize);
    }

private:
    /// The process's memory counters, all zero when the call fails.
    static PROCESS_MEMORY_COUNTERS counters() noexcept {
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return PROCESS_MEMORY_COUNTERS{};
        return pmc;
    }
};

}  // namespace pygim::platform
