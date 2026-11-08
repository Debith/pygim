#pragma once
// platform/posix/files.h — a path's text as POSIX keeps it: the system's own bytes, so a name that
// is not UTF-8 round-trips. fs::path::string() and a path made from a std::string convert nothing
// here. Standard library alone, so it compiles on every system; platform/files.h chooses it where
// the family is POSIX (D106).
//
// CORE layer: pybind-free.

#include <filesystem>
#include <string>
#include <string_view>

namespace pygim::platform {

struct posix_files {
    [[nodiscard]] static std::string text_from_fs(const std::filesystem::path& p) { return p.string(); }
    [[nodiscard]] static std::filesystem::path fs_from_text(std::string_view text) { return std::filesystem::path(std::string(text)); }
};

}  // namespace pygim::platform
