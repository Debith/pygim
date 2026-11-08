#pragma once
// platform/files.h — a path's text as the system takes it. Infrastructure behind the platform
// boundary (docs/design/platform_boundary.md, D77): pathlike's files (pathlike/core.h) call these
// and never ask which system they run on. One class per system, each in a file of its own that asks
// nothing (D106): posix/files.h keeps the system's own bytes, windows/files.h UTF-8 from the wide
// native form. Both use the standard library alone, so both are compiled and proven on every system,
// and the family chooses one here, once.
//
//     text_from_fs(p)    -> the internal text
//     fs_from_text(t)    -> the std::filesystem path that text names
//
// CORE layer: pybind-free.

#include <concepts>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>

#include "native.h"
#include "posix/files.h"
#include "windows/files.h"

namespace pygim::platform {

namespace fs = std::filesystem;

/// What a system's files class provides: a path's internal text, and the path a text names.
template <class F>
concept path_text_rules = requires(const fs::path& p, std::string_view t) {
    { F::text_from_fs(p) } -> std::same_as<std::string>;
    { F::fs_from_text(t) } -> std::same_as<fs::path>;
};
static_assert(path_text_rules<posix_files> && path_text_rules<windows_files>);

/// The running system's rules, chosen once by the family (native.h).
using native_files = std::conditional_t<native == family::windows, windows_files, posix_files>;

[[nodiscard]] inline std::string text_from_fs(const fs::path& p) { return native_files::text_from_fs(p); }
[[nodiscard]] inline fs::path fs_from_text(std::string_view text) { return native_files::fs_from_text(text); }

}  // namespace pygim::platform
