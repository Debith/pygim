#pragma once
// platform/posix/libraries.h — loading a shared library as POSIX does it, behind platform/
// libraries.h's library_loader: dlopen, dlsym, and glob for the newest installed file. Only that
// header includes it, and only where the system is not Windows: nothing here asks which system it
// runs on (D106).
//
// CORE layer: pybind-free.

#include <dlfcn.h>
#include <glob.h>

namespace pygim::platform {

class posix_loader {
public:
    /// The library `name` names — by the loader's search path, or as a path — resolved now and its
    /// symbols global; nullptr when the loader finds none.
    [[nodiscard]] static void* open(const char* name) noexcept { return ::dlopen(name, RTLD_NOW | RTLD_GLOBAL); }

    /// The function `name` in the library; nullptr when it has none of that name.
    [[nodiscard]] static auto symbol(void* handle, const char* name) noexcept -> void (*)() {
        return reinterpret_cast<void (*)()>(::dlsym(handle, name));
    }

    /// The last file `pattern` matches, in glob's sorted order — the highest version installed —
    /// opened; nullptr when nothing matches or it does not open.
    [[nodiscard]] static void* newest(const char* pattern) noexcept {
        glob_t g{};
        void* handle = nullptr;
        if (::glob(pattern, 0, nullptr, &g) == 0 && g.gl_pathc > 0) handle = open(g.gl_pathv[g.gl_pathc - 1]);
        ::globfree(&g);
        return handle;
    }
};

}  // namespace pygim::platform
