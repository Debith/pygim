#pragma once
// platform/macos/caches.h — the data caches as macOS's sysctl reports them. Only platform/caches.h
// includes this, and only on macOS: nothing here asks which system it runs on.
//
// Apple silicon reports its caches per performance level; the fast cores' are the ones a hot loop
// gets, so perflevel0's come first, the machine-wide names where it has none (Intel). It reports no
// L3 there, and cpu.h then takes the L2 or a typical size, the larger.
//
// CORE layer: pybind-free.

#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>

namespace pygim::platform {

struct macos_caches {
    [[nodiscard]] static reported_caches system_caches() noexcept {
        const std::size_t l1 = size_of("hw.perflevel0.l1dcachesize"), l2 = size_of("hw.perflevel0.l2cachesize");
        return {l1 ? l1 : size_of("hw.l1dcachesize"), l2 ? l2 : size_of("hw.l2cachesize"), size_of("hw.l3cachesize"), size_of("hw.cachelinesize")};
    }

private:
    /// A sysctl number by name, 32 or 64 bits as the system writes it; 0 when the name is not there.
    [[nodiscard]] static std::size_t size_of(const char* name) noexcept {
        std::uint64_t v = 0;
        std::size_t len = sizeof v;
        if (::sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
        return len == sizeof(std::uint32_t) ? static_cast<std::size_t>(static_cast<std::uint32_t>(v)) : static_cast<std::size_t>(v);
    }
};

}  // namespace pygim::platform
