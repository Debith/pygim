#pragma once
// platform/windows/libraries.h — loading a shared library as Windows does it, behind platform/
// libraries.h's library_loader: LoadLibraryW and GetProcAddress. Only that header includes it, and
// only on Windows: nothing here asks which system it runs on (D106).
//
// CORE layer: pybind-free.

#include <string>

#include "../windows_headers.h"

namespace pygim::platform {

class windows_loader {
public:
    /// The library `name` names, by the loader's search order or as a path; nullptr when it finds none.
    [[nodiscard]] static void* open(const char* name) noexcept {
        const std::string narrow(name);   // the names are ASCII: each char is its wide one
        const std::wstring wide(narrow.begin(), narrow.end());
        return static_cast<void*>(::LoadLibraryW(wide.c_str()));
    }

    /// The function `name` in the library; nullptr when it has none of that name.
    [[nodiscard]] static auto symbol(void* handle, const char* name) noexcept -> void (*)() {
        return reinterpret_cast<void (*)()>(::GetProcAddress(static_cast<HMODULE>(handle), name));
    }

    /// None: Windows has no glob, and its loader's search order finds what is installed.
    [[nodiscard]] static void* newest(const char*) noexcept { return nullptr; }
};

}  // namespace pygim::platform
