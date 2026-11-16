#pragma once
// platform/windows/files.h — a path's text as Windows keeps it: UTF-8, lossless from the wide native
// form and what Python's str carries. fs::path::string() would narrow through the ACP there and can
// mangle a non-ASCII name, so the text goes through u8string both ways. Standard library alone, so
// it compiles on every system; platform/files.h chooses it where the family is Windows (D106).
//
// CORE layer: pybind-free.

#include <filesystem>
#include <string>
#include <string_view>

namespace pygim::platform {

struct windows_files {
    [[nodiscard]] static std::string text_from_fs(const std::filesystem::path& p) {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    }
    [[nodiscard]] static std::filesystem::path fs_from_text(std::string_view text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
};

}  // namespace pygim::platform
