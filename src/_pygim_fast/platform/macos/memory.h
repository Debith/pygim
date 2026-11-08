#pragma once
// platform/macos/memory.h — the calls macOS's memory probes make, one thin wrapper each, behind
// platform/memory.h's macos_memory_calls: task_info on the calling task, getrusage. No logic lives
// here — that is basic_macos_memory's, proven over fake calls on every system (D138). Only
// platform/memory.h includes this, and only on macOS: nothing here asks which system it runs on
// (D106).
//
// Cost: one Mach call for the current reading, one getrusage for the peak.
//
// CORE layer: pybind-free.

#include <mach/mach.h>
#include <sys/resource.h>

#include <cstdint>
#include <optional>

namespace pygim::platform {

struct macos_mach_calls {
    /// task_info(MACH_TASK_BASIC_INFO) resident_size: the bytes of this process in physical memory.
    [[nodiscard]] static std::optional<std::uint64_t> task_resident_size() noexcept {
        mach_task_basic_info info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return std::nullopt;
        return static_cast<std::uint64_t>(info.resident_size);
    }
    /// getrusage(RUSAGE_SELF) ru_maxrss as the kernel gives it (bytes here); nothing when it fails.
    [[nodiscard]] static std::optional<std::uint64_t> max_rss() noexcept {
        rusage ru{};
        if (getrusage(RUSAGE_SELF, &ru) != 0) return std::nullopt;
        return static_cast<std::uint64_t>(ru.ru_maxrss);
    }
};

using macos_memory = basic_macos_memory<macos_mach_calls>;

}  // namespace pygim::platform
