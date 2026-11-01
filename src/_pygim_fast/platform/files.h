#pragma once
// platform/files.h — a path's text as the system takes it. Infrastructure behind the platform
// boundary (docs/design/platform_boundary.md, D77): pathlike's files (pathlike/core.h) and the
// Python boundary (pathlike/adapter/adapter.h) call these and never ask which system they run on.
//
//     text_from_fs(p)    -> the internal text: the system's own bytes on POSIX, UTF-8 on Windows
//     fs_from_text(t)    -> the std::filesystem path that text names
//
// CORE layer: pybind-free.

#include <filesystem>
#include <string>
#include <string_view>

#include "native.h"

namespace pygim::platform {

namespace fs = std::filesystem;

/// What a path's internal text is: the system's own bytes on POSIX (so a name that is not UTF-8
/// round-trips), UTF-8 on Windows (lossless from the wide native form, and what Python's str
/// carries). The Python boundary encodes a str by it (pathlike/adapter/adapter.h).
inline constexpr bool path_text_is_utf8 = native == family::windows;

// The OS boundary: the internal text form, as path_text_is_utf8 says. fs::path::string() would
// narrow through the ACP on Windows and can mangle non-ASCII paths.
[[nodiscard]] inline std::string text_from_fs(const fs::path& p) {
#ifdef _WIN32
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.native();
#endif
}

[[nodiscard]] inline fs::path fs_from_text(std::string_view text) {
#ifdef _WIN32
    return fs::path(std::u8string(text.begin(), text.end()));
#else
    return fs::path(std::string(text));
#endif
}

}  // namespace pygim::platform
