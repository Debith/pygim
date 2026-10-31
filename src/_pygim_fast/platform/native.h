#pragma once
// platform/native.h — the family of operating systems this build follows, decided once: the one
// place that asks the compiler. Everything else chooses by this value, never by an #if
// (docs/design/platform_boundary.md, D77): every family's rules are compiled on every system, so
// the domain is proven and tested under each of them on any.
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
