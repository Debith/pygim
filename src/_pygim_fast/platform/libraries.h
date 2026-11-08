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
// Each system's class is a template on the calls it makes (D138): the logic — which name to open,
// what the newest match is, what no match answers — lives here, in basic_posix_loader and
// basic_windows_loader, compiled and proven on every system over fake calls
// (tests/static/platform_proofs.cpp); the calls themselves are one thin wrapper each, in a file of
// the system's own that asks nothing (D106): posix/libraries.h (dlopen, dlsym, glob) and
// windows/libraries.h (LoadLibraryW, GetProcAddress). This header picks the running system's once
// and writes `library` over it. The policy is a type, so the shipped class is the logic with the
// calls inlined: nothing of this is seen at run time.
//
// A library is never unloaded: what it gave stays callable for the process's life.
//
// CORE layer: pybind-free.

#include <concepts>
#include <span>
#include <string>
#include <vector>

namespace pygim::platform {

/// A function's address as a library hands it out, before it is cast to its own type.
using symbol_address = void (*)();

/// The calls a POSIX loader makes, one wrapper each: dlopen now and global, dlsym, and glob's
/// sorted matches. `handle` is what dlopen hands back, `none` its answer for nothing found.
template <class C>
concept posix_library_calls = requires(const char* text, typename C::handle h) {
    typename C::handle;
    { C::none } -> std::convertible_to<typename C::handle>;
    { C::dlopen_now(text) } -> std::same_as<typename C::handle>;
    { C::dlsym_of(h, text) } -> std::same_as<symbol_address>;
    { C::glob_sorted(text) } -> std::same_as<std::vector<std::string>>;
};

/// The calls a Windows loader makes: LoadLibraryW on a wide name, GetProcAddress.
template <class C>
concept windows_library_calls = requires(const wchar_t* wide, const char* text, typename C::handle h) {
    typename C::handle;
    { C::none } -> std::convertible_to<typename C::handle>;
    { C::load_library(wide) } -> std::same_as<typename C::handle>;
    { C::proc_address(h, text) } -> std::same_as<symbol_address>;
};

/// What every system's loader answers: a handle or an address, `none` or nullptr when there is none.
template <class L>
concept library_loader = requires(const char* text, typename L::handle h) {
    typename L::handle;
    { L::none } -> std::convertible_to<typename L::handle>;
    { L::open(text) } -> std::same_as<typename L::handle>;
    { L::symbol(h, text) } -> std::same_as<symbol_address>;
    { L::newest(text) } -> std::same_as<typename L::handle>;
};

/// POSIX's loader: the name as given to dlopen, the newest file the last of glob's sorted matches.
template <posix_library_calls Calls>
class basic_posix_loader {
public:
    using handle = typename Calls::handle;
    static constexpr handle none = Calls::none;

    /// The library `name` names — by the loader's search path, or as a path — resolved now and its
    /// symbols global; none when the loader finds nothing.
    [[nodiscard]] static constexpr handle open(const char* name) noexcept { return Calls::dlopen_now(name); }
    /// The function `name` in the library; nullptr when it has none of that name.
    [[nodiscard]] static constexpr symbol_address symbol(handle h, const char* name) noexcept { return Calls::dlsym_of(h, name); }
    /// The last file `pattern` matches, in glob's sorted order — the highest version installed —
    /// opened; none when nothing matches or it does not open.
    [[nodiscard]] static constexpr handle newest(const char* pattern) {
        const std::vector<std::string> files = Calls::glob_sorted(pattern);
        return files.empty() ? none : open(files.back().c_str());
    }
};

/// Windows' loader: the narrow name widened char for char (the names are ASCII), no glob.
template <windows_library_calls Calls>
class basic_windows_loader {
public:
    using handle = typename Calls::handle;
    static constexpr handle none = Calls::none;

    /// The library `name` names, by the loader's search order or as a path; none when it finds nothing.
    [[nodiscard]] static constexpr handle open(const char* name) {
        std::wstring wide;
        for (const char* p = name; *p != '\0'; ++p) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
        return Calls::load_library(wide.c_str());
    }
    /// The function `name` in the library; nullptr when it has none of that name.
    [[nodiscard]] static constexpr symbol_address symbol(handle h, const char* name) noexcept { return Calls::proc_address(h, name); }
    /// None: Windows has no glob, and its loader's search order finds what is installed.
    [[nodiscard]] static constexpr handle newest(const char*) noexcept { return none; }
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

/// A library over a loader: found by the first of several names or the newest match, asked for
/// its functions. `library` is the running system's.
template <library_loader Loader>
class basic_library {
public:
    using handle = typename Loader::handle;

    constexpr basic_library() noexcept = default;

    /// The first of `names` the system's loader finds (its search path, or a path as given).
    [[nodiscard]] static constexpr basic_library first_of(std::span<const char* const> names) {
        for (const char* name : names) {
            if (basic_library lib{Loader::open(name)}) return lib;
        }
        return {};
    }

    /// The last file `pattern` matches, in POSIX glob's sorted order — the highest version
    /// installed. None on Windows, which has no glob.
    [[nodiscard]] static constexpr basic_library last_match(const char* pattern) { return basic_library{Loader::newest(pattern)}; }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_handle != Loader::none; }

    /// The function `name` as an `F`; nullptr when the library has none of that name.
    template <class F>
    [[nodiscard]] F function(const char* name) const noexcept {
        return reinterpret_cast<F>(address_of(name));
    }

    /// The function `name`'s address as the loader hands it out, nullptr when there is none: what
    /// function<F> casts, and what a proof can compare.
    [[nodiscard]] constexpr symbol_address address_of(const char* name) const noexcept {
        return *this ? Loader::symbol(m_handle, name) : nullptr;
    }

private:
    constexpr explicit basic_library(handle h) noexcept : m_handle(h) {}

    handle m_handle = Loader::none;
};

using library = basic_library<native_loader>;

}  // namespace pygim::platform
