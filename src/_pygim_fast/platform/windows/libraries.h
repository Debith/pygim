#pragma once
// platform/windows/libraries.h — the calls a Windows loader makes, one thin wrapper each:
// LoadLibraryW and GetProcAddress. The logic over them is basic_windows_loader's, in
// platform/libraries.h, proven over fake calls on every system (D138). Only that header includes
// this, and only on Windows: nothing here asks which system it runs on (D106).
//
// CORE layer: pybind-free.

#include "../windows_headers.h"

namespace pygim::platform {

struct win32_library_calls {
    [[nodiscard]] static void* load_library(const wchar_t* name) noexcept { return static_cast<void*>(::LoadLibraryW(name)); }
    [[nodiscard]] static symbol_address proc_address(void* h, const char* name) noexcept {
        return reinterpret_cast<symbol_address>(::GetProcAddress(static_cast<HMODULE>(h), name));
    }
};

using windows_loader = basic_windows_loader<win32_library_calls>;

}  // namespace pygim::platform
