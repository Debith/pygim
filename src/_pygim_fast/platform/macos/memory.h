#pragma once
// platform/macos/memory.h — the process's memory as macOS keeps it, behind platform/memory.h's
// process_memory. Only that header includes it, and only on macOS: nothing here asks which system
// it runs on (D106).
//
// Cost: one Mach call for the current reading, one getrusage for the peak.
//
// CORE layer: pybind-free.

#include <mach/mach.h>
#include <sys/resource.h>

#include <cstdint>

namespace pygim::platform {

class macos_memory {
public:
    /// task_info(MACH_TASK_BASIC_INFO) on the calling task, resident_size — the bytes of this process
    /// in physical memory. One Mach call.
    [[nodiscard]] static std::uint64_t resident_bytes() noexcept {
        mach_task_basic_info info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
        return static_cast<std::uint64_t>(info.resident_size);
    }

    /// getrusage(RUSAGE_SELF) ru_maxrss, which the Darwin kernel reports in BYTES (Linux reports the
    /// same field in kilobytes). The high-water mark of the resident size.
    [[nodiscard]] static std::uint64_t peak_resident_bytes() noexcept {
        rusage ru{};
        if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
        return static_cast<std::uint64_t>(ru.ru_maxrss);   // bytes on macOS
    }
};

}  // namespace pygim::platform
