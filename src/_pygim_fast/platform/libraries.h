#pragma once
// platform/libraries.h — a shared library loaded at run time and its functions looked up.
// Infrastructure behind the platform boundary (docs/design/platform_boundary.md, D77): the BCP
// loader (persistence/strategy/mssql/bcp/bcp_api.h) names the driver's files, as each system names
// them, and never asks which system it runs on.
//
//     const library lib = library::first_of(names);           // the first the system's loader finds
//     if (!lib) lib = library::last_match("/opt/x*/lib*.so.*");  // or the newest installed file
//     auto init = lib.function<init_fn>("init");              // nullptr when it is not there
//
// What differs by system is three calls — open a file, find a name in it, find the newest file a
// pattern matches — one class per system, each in a file of its own that asks nothing (D106):
// posix/libraries.h (dlopen, dlsym, glob) and windows/libraries.h (LoadLibraryW, GetProcAddress).
// This header states what both answer, picks the running system's once, and writes `library`
// over it.
//
// A library is never unloaded: what it gave stays callable for the process's life.
//
// CORE layer: pybind-free.

#include <concepts>
#include <span>

namespace pygim::platform {

/// A function's address as a library hands it out, before it is cast to its own type.
using symbol_address = void (*)();

/// What every system's loader answers: a handle or an address, nullptr when there is none.
template <class L>
concept library_loader = requires(const char* text, void* handle) {
    { L::open(text) } noexcept -> std::same_as<void*>;
    { L::symbol(handle, text) } noexcept -> std::same_as<symbol_address>;
    { L::newest(text) } noexcept -> std::same_as<void*>;
};

}  // namespace pygim::platform

// The one choice: each system's file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/libraries.h"
namespace pygim::platform { using native_loader = windows_loader; }
#else
#include "posix/libraries.h"
namespace pygim::platform { using native_loader = posix_loader; }
#endif

namespace pygim::platform {

static_assert(library_loader<native_loader>);

class library {
public:
    constexpr library() noexcept = default;

    /// The first of `names` the system's loader finds (its search path, or a path as given).
    [[nodiscard]] static library first_of(std::span<const char* const> names) noexcept {
        for (const char* name : names) {
            if (library lib{native_loader::open(name)}) return lib;
        }
        return {};
    }

    /// The last file `pattern` matches, in POSIX glob's sorted order — the highest version
    /// installed. None on Windows, which has no glob.
    [[nodiscard]] static library last_match(const char* pattern) noexcept { return library{native_loader::newest(pattern)}; }

    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

    /// The function `name` as an `F`; nullptr when the library has none of that name.
    template <class F>
    [[nodiscard]] F function(const char* name) const noexcept {
        if (m_handle == nullptr) return nullptr;
        return reinterpret_cast<F>(native_loader::symbol(m_handle, name));
    }

private:
    explicit library(void* handle) noexcept : m_handle(handle) {}

    void* m_handle = nullptr;
};

}  // namespace pygim::platform
