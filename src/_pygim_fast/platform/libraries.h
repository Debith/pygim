#pragma once
// platform/libraries.h — a shared library loaded at run time and its functions looked up:
// LoadLibraryW and GetProcAddress on Windows, dlopen and dlsym elsewhere. Infrastructure behind
// the platform boundary (docs/design/platform_boundary.md, D77): the BCP loader
// (persistence/strategy/mssql/bcp/bcp_api.h) names the driver's files, as each system names them,
// and never asks which system it runs on.
//
//     const library lib = library::first_of(names);           // the first the system's loader finds
//     if (!lib) lib = library::last_match("/opt/x*/lib*.so.*");  // or the newest installed file
//     auto init = lib.function<init_fn>("init");              // nullptr when it is not there
//
// A library is never unloaded: what it gave stays callable for the process's life.
//
// CORE layer: pybind-free.

#include <span>
#include <string>

#include "windows_headers.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <glob.h>
#endif

namespace pygim::platform {

class library {
public:
    constexpr library() noexcept = default;

    /// The first of `names` the system's loader finds (its search path, or a path as given).
    [[nodiscard]] static library first_of(std::span<const char* const> names) noexcept {
        for (const char* name : names) {
            if (library lib = open(name)) return lib;
        }
        return {};
    }

    /// The last file `pattern` matches, in POSIX glob's sorted order — the highest version
    /// installed. None on Windows, which has no glob.
    [[nodiscard]] static library last_match(const char* pattern) noexcept {
#ifdef _WIN32
        (void)pattern;
        return {};
#else
        glob_t g{};
        library lib;
        if (::glob(pattern, 0, nullptr, &g) == 0 && g.gl_pathc > 0) lib = open(g.gl_pathv[g.gl_pathc - 1]);
        ::globfree(&g);
        return lib;
#endif
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

    /// The function `name` as an `F`; nullptr when the library has none of that name.
    template <class F>
    [[nodiscard]] F function(const char* name) const noexcept {
        if (m_handle == nullptr) return nullptr;
#ifdef _WIN32
        return reinterpret_cast<F>(::GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
        return reinterpret_cast<F>(::dlsym(m_handle, name));
#endif
    }

private:
    explicit library(void* handle) noexcept : m_handle(handle) {}

    [[nodiscard]] static library open(const char* name) noexcept {
#ifdef _WIN32
        const std::string narrow(name);   // the names are ASCII: each char is its wide one
        const std::wstring wide(narrow.begin(), narrow.end());
        return library(static_cast<void*>(::LoadLibraryW(wide.c_str())));
#else
        return library(::dlopen(name, RTLD_NOW | RTLD_GLOBAL));
#endif
    }

    void* m_handle = nullptr;
};

}  // namespace pygim::platform
