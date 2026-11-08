#pragma once
// platform/windows/libraries.h — the calls a Windows loader makes, one thin wrapper each, behind
// platform/libraries.h's windows_library_calls: LoadLibraryW and GetProcAddress. No logic lives here
// — that is basic_windows_loader's, proven over fake calls on every system (D138). Only
// platform/libraries.h includes this, and only on Windows: nothing here asks which system it runs
// on (D106).
//
// CORE layer: pybind-free.

#include "../windows_headers.h"

namespace pygim::platform {

struct win32_library_calls {
    using handle = void*;   // an HMODULE
    static constexpr handle none = nullptr;

    [[nodiscard]] static void* load_library(const wchar_t* name) noexcept { return static_cast<void*>(::LoadLibraryW(name)); }
    [[nodiscard]] static symbol_address proc_address(void* h, const char* name) noexcept {
        return reinterpret_cast<symbol_address>(::GetProcAddress(static_cast<HMODULE>(h), name));
    }
};

using windows_loader = basic_windows_loader<win32_library_calls>;

}  // namespace pygim::platform
