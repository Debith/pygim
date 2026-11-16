#pragma once
// platform/linux/caches.h — the calls Linux's cache reading makes, one thin wrapper each: sysconf's
// cache sizes, and the text of one file of the kernel's cache list. The logic over them is
// basic_linux_caches', in platform/caches.h, proven over fake calls on every system. Only that header
// includes this, and only where the system is neither Windows nor macOS: nothing here asks which
// system it runs on.
//
// CORE layer: pybind-free.

#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace pygim::platform {

struct linux_cache_calls {
    /// sysconf's L1d, L2, L3 and L1 line, 0 for each it does not report. glibc names them; a C
    /// library that does not (musl) reports none, and the kernel's list is read instead.
    [[nodiscard]] static reported_caches sysconf_caches() noexcept {
#if defined(_SC_LEVEL1_DCACHE_SIZE) && defined(_SC_LEVEL2_CACHE_SIZE) && defined(_SC_LEVEL3_CACHE_SIZE) && defined(_SC_LEVEL1_DCACHE_LINESIZE)
        return {conf(_SC_LEVEL1_DCACHE_SIZE), conf(_SC_LEVEL2_CACHE_SIZE), conf(_SC_LEVEL3_CACHE_SIZE), conf(_SC_LEVEL1_DCACHE_LINESIZE)};
#else
        return {};
#endif
    }

    /// What /sys/devices/system/cpu/cpu0/cache/index<index>/<file> says ("512K", "Data"), read into
    /// `buffer`; empty when it is not there.
    [[nodiscard]] static std::string_view sys_cache_text(int index, const char* file, std::array<char, 32>& buffer) noexcept {
        char path[96];
        std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu0/cache/index%d/%s", index, file);
        std::FILE* f = std::fopen(path, "re");   // e: close on exec, so a child exec'd from Python does not inherit it
        if (f == nullptr) return {};
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), f);
        std::fclose(f);
        return {buffer.data(), got};
    }

private:
    [[nodiscard]] static std::size_t conf(int name) noexcept {
        const long v = ::sysconf(name);
        return v > 0 ? static_cast<std::size_t>(v) : std::size_t{0};
    }
};

using linux_caches = basic_linux_caches<linux_cache_calls>;

}  // namespace pygim::platform
