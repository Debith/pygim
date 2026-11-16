#pragma once
// platform/windows/caches.h — the data caches as Windows' GetLogicalProcessorInformationEx reports
// them: each level's largest cache that holds data, and the L1's line. Only platform/caches.h
// includes this, and only on Windows: nothing here asks which system it runs on.
//
// CORE layer: pybind-free.

#include "../windows_headers.h"

#include <array>
#include <cstddef>
#include <vector>

namespace pygim::platform {

struct windows_caches {
    [[nodiscard]] static reported_caches system_caches() noexcept {
        DWORD bytes = 0;
        GetLogicalProcessorInformationEx(RelationCache, nullptr, &bytes);
        if (bytes == 0) return {};
        std::vector<unsigned char> buffer(bytes);
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
        if (!GetLogicalProcessorInformationEx(RelationCache, info, &bytes)) return {};
        std::array<std::size_t, 4> size{};   // [1..3] the levels
        std::size_t line = 0;
        for (DWORD at = 0; at < bytes;) {
            const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
            const CACHE_RELATIONSHIP& c = entry->Cache;
            if (entry->Relationship == RelationCache && c.Level >= 1 && c.Level <= 3 && (c.Type == CacheData || c.Type == CacheUnified)) {
                if (c.CacheSize > size[c.Level]) size[c.Level] = c.CacheSize;
                if (c.Level == 1) line = c.LineSize;
            }
            at += entry->Size;
        }
        return {size[1], size[2], size[3], line};
    }
};

}  // namespace pygim::platform
