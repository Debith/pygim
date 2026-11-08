#pragma once
// platform/posix/libraries.h — the calls a POSIX loader makes, one thin wrapper each: dlopen, dlsym,
// glob. The logic over them is basic_posix_loader's, in platform/libraries.h, proven over fake calls
// on every system (D138). Only that header includes this, and only where the system is not Windows:
// nothing here asks which system it runs on (D106).
//
// CORE layer: pybind-free.

#include <dlfcn.h>
#include <glob.h>

#include <cstddef>
#include <string>
#include <vector>

namespace pygim::platform {

struct posix_dl_calls {
    /// dlopen, resolved now and its symbols global; nullptr when the loader finds nothing.
    [[nodiscard]] static void* dlopen_now(const char* name) noexcept { return ::dlopen(name, RTLD_NOW | RTLD_GLOBAL); }
    [[nodiscard]] static symbol_address dlsym_of(void* h, const char* name) noexcept { return reinterpret_cast<symbol_address>(::dlsym(h, name)); }
    /// Every file `pattern` matches, in glob's sorted order; empty when nothing matches.
    [[nodiscard]] static std::vector<std::string> glob_sorted(const char* pattern) noexcept {
        glob_t g{};
        std::vector<std::string> files;
        if (::glob(pattern, 0, nullptr, &g) == 0) files.assign(g.gl_pathv, g.gl_pathv + g.gl_pathc);
        ::globfree(&g);
        return files;
    }
};

using posix_loader = basic_posix_loader<posix_dl_calls>;

}  // namespace pygim::platform
