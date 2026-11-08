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
// The logic a system puts above its calls lives here, as templates on those calls (D138):
// basic_posix_loader (the newest file is the last of glob's sorted matches) and basic_windows_loader
// (a name widened, no glob), and basic_library over either (the first name that opens). It compiles
// on every system and is proven there over fake calls (tests/static/platform_proofs.cpp). The calls
// are one thin wrapper each, in a file of the system's own that asks nothing (D106):
// posix/libraries.h (dlopen, dlsym, glob) and windows/libraries.h (LoadLibraryW, GetProcAddress).
// They are a type whose static functions inline, so what runs is the logic with the calls in place.
//
// A library is never unloaded: what it gave stays callable for the process's life.
//
// CORE layer: pybind-free.

#include <concepts>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

/// POSIX: a name opened as given; the newest file is the last of glob's sorted matches.
template <class Calls>
struct basic_posix_loader {
    [[nodiscard]] static constexpr void* open(const char* name) noexcept { return Calls::dlopen_now(name); }
    [[nodiscard]] static constexpr symbol_address symbol(void* h, const char* name) noexcept { return Calls::dlsym_of(h, name); }
    [[nodiscard]] static constexpr void* newest(const char* pattern) noexcept {
        const std::vector<std::string> files = Calls::glob_sorted(pattern);
        return files.empty() ? nullptr : open(files.back().c_str());
    }
};

/// Windows: a name widened char for char (the names are ASCII); no glob, so never a newest match.
template <class Calls>
struct basic_windows_loader {
    [[nodiscard]] static constexpr void* open(const char* name) noexcept {
        const std::string_view narrow(name);
        return Calls::load_library(std::wstring(narrow.begin(), narrow.end()).c_str());
    }
    [[nodiscard]] static constexpr symbol_address symbol(void* h, const char* name) noexcept { return Calls::proc_address(h, name); }
    [[nodiscard]] static constexpr void* newest(const char*) noexcept { return nullptr; }
};

}  // namespace pygim::platform

// The one choice: each system's calls file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/libraries.h"
namespace pygim::platform { using native_loader = windows_loader; }
#else
#include "posix/libraries.h"
namespace pygim::platform { using native_loader = posix_loader; }
#endif

namespace pygim::platform {

static_assert(library_loader<native_loader>);

/// A library over a loader: the first of several names that opens, or the newest match, asked for its
/// functions. `library` is the running system's.
template <library_loader Loader>
class basic_library {
public:
    constexpr basic_library() noexcept = default;

    /// The first of `names` the system's loader finds (its search path, or a path as given).
    [[nodiscard]] static constexpr basic_library first_of(std::span<const char* const> names) noexcept {
        for (const char* name : names) {
            if (basic_library lib{Loader::open(name)}) return lib;
        }
        return {};
    }

    /// The last file `pattern` matches, in POSIX glob's sorted order — the highest version
    /// installed. None on Windows, which has no glob.
    [[nodiscard]] static constexpr basic_library last_match(const char* pattern) noexcept { return basic_library{Loader::newest(pattern)}; }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_handle != nullptr; }

    /// The function `name` as an `F`; nullptr when the library has none of that name.
    template <class F>
    [[nodiscard]] F function(const char* name) const noexcept {
        return reinterpret_cast<F>(address_of(name));
    }

    /// The function `name`'s address as the loader hands it out, nullptr when there is none: what
    /// function<F> casts, and what a proof compares.
    [[nodiscard]] constexpr symbol_address address_of(const char* name) const noexcept { return *this ? Loader::symbol(m_handle, name) : nullptr; }

private:
    constexpr explicit basic_library(void* handle) noexcept : m_handle(handle) {}

    void* m_handle = nullptr;
};

using library = basic_library<native_loader>;

}  // namespace pygim::platform
