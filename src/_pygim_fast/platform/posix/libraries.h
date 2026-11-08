#pragma once
// platform/posix/libraries.h — the calls a POSIX loader makes, one thin wrapper each, behind
// platform/libraries.h's posix_library_calls: dlopen, dlsym, glob. No logic lives here — that is
// basic_posix_loader's, proven over fake calls on every system (D138). Only platform/libraries.h
// includes this, and only where the system is not Windows: nothing here asks which system it runs
// on (D106).
//
// CORE layer: pybind-free.

#include <dlfcn.h>
#include <glob.h>

#include <cstddef>
#include <string>
#include <vector>

namespace pygim::platform {

struct posix_dl_calls {
    using handle = void*;
    static constexpr handle none = nullptr;

    [[nodiscard]] static void* dlopen_now(const char* name) noexcept { return ::dlopen(name, RTLD_NOW | RTLD_GLOBAL); }
    [[nodiscard]] static symbol_address dlsym_of(void* h, const char* name) noexcept { return reinterpret_cast<symbol_address>(::dlsym(h, name)); }
    /// Every file `pattern` matches, in glob's sorted order; empty when nothing matches.
    [[nodiscard]] static std::vector<std::string> glob_sorted(const char* pattern) {
        glob_t g{};
        std::vector<std::string> files;
        if (::glob(pattern, 0, nullptr, &g) == 0) {
            for (std::size_t i = 0; i < g.gl_pathc; ++i) files.emplace_back(g.gl_pathv[i]);
        }
        ::globfree(&g);
        return files;
    }
};

using posix_loader = basic_posix_loader<posix_dl_calls>;

}  // namespace pygim::platform
