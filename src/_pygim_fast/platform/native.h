#pragma once
// platform/native.h — the family of operating systems this build follows, decided once. Outside
// platform/, everything chooses by this value, never by an #if (docs/design/platform_boundary.md,
// D77): every family's rules are compiled on every system, so the domain is proven and tested under
// each of them on any. Inside platform/, a call written against one system's own headers and types
// is the only code left that asks the compiler.
//
//     using rules = std::conditional_t<platform::native == platform::family::windows, windows_rules, posix_rules>;
//
// CORE layer: pybind-free.

namespace pygim::platform {

/// A family of systems with one set of rules: POSIX's (Linux, macOS, the BSDs) or Windows'.
enum class family { posix, windows };

/// The running system's family.
inline constexpr family native =
#ifdef _WIN32
    family::windows;
#else
    family::posix;
#endif
static_assert(native == family::posix || native == family::windows);

}  // namespace pygim::platform
