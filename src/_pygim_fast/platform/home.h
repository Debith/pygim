#pragma once
// platform/home.h — whose home a path's "~" names, as each system says. Infrastructure behind the
// platform boundary (docs/design/platform_boundary.md, D77): pathlike's expanduser (pathlike/core.h)
// asks home_of and never which system it runs on.
//
//     home_of("")      -> "/home/someone"      $HOME, or this user's entry in the password database
//     home_of("root")  -> "/root"              that user's entry
//     home_of("ghost") -> nullopt              no such user: pathlib's "Could not determine home directory"
//
// The rules are posixpath's and ntpath's expanduser, compiled on every system and proven here:
// POSIX drops a home's trailing '/' and takes an empty one as the root; Windows reads USERPROFILE,
// or HOMEDRIVE + HOMEPATH, and guesses another user's home beside this user's, but only where this
// user's folder is named after them (C:\Users\someone -> C:\Users\other). The calls are each
// system's: getenv and getpwuid_r / getpwnam_r; _wgetenv, its text made UTF-8.
//
// CORE layer: pybind-free.

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "windows_headers.h"

#ifdef _WIN32
#include <cwchar>
#else
#include <cerrno>
#include <pwd.h>
#include <unistd.h>
#include <vector>
#endif

namespace pygim::platform {

/// posixpath's rule for a home's text: its trailing '/' dropped, an empty one the root.
[[nodiscard]] constexpr std::string posix_home(std::string home) {
    while (!home.empty() && home.back() == '/') home.pop_back();
    return home.empty() ? std::string("/") : home;
}
static_assert(posix_home("/home/someone/") == "/home/someone" && posix_home("") == "/" && posix_home("//") == "/");

/// ntpath's join of HOMEDRIVE and HOMEPATH: the drive, then the path, unless the path has a drive of its own.
[[nodiscard]] constexpr std::string windows_join(std::string_view drive, std::string_view path) {
    if (path.size() >= 2 && path[1] == ':') return std::string(path);
    return std::string(drive) + std::string(path);
}
static_assert(windows_join("C:", "\\Users\\someone") == "C:\\Users\\someone" && windows_join("", "\\Users\\x") == "\\Users\\x" &&
              windows_join("C:", "D:\\x") == "D:\\x");

/// ntpath's rule for `user`'s home given this user's: this user's own ("" or their name), or the
/// folder beside it named `user` when this user's folder is named after them; no guess otherwise.
[[nodiscard]] constexpr std::optional<std::string> windows_home_of(std::string_view user, std::string_view home,
                                                                   std::optional<std::string_view> current_user) {
    if (user.empty() || (current_user && user == *current_user)) return std::string(home);
    const std::size_t drive = home.size() >= 2 && home[1] == ':' ? 2 : 0;
    const std::size_t sep = home.find_last_of("\\/");
    const std::size_t name_at = sep == std::string_view::npos || sep < drive ? drive : sep + 1;
    if (!current_user || home.substr(name_at) != *current_user) return std::nullopt;   // a profile not named after its user
    std::size_t head = name_at;
    while (head > drive && (home[head - 1] == '\\' || home[head - 1] == '/')) --head;
    if (head == drive && name_at > drive) head = drive + 1;                              // the root stays: C:\someone -> C:\other
    std::string beside(home.substr(0, head));
    if (!beside.empty() && beside.back() != '\\' && beside.back() != '/' && beside != home.substr(0, drive)) beside += '\\';
    return beside + std::string(user);
}
static_assert(windows_home_of("", "C:\\Users\\someone", "someone") == "C:\\Users\\someone" &&
              windows_home_of("someone", "C:\\Users\\someone", "someone") == "C:\\Users\\someone" &&
              windows_home_of("other", "C:\\Users\\someone", "someone") == "C:\\Users\\other" &&
              windows_home_of("other", "C:\\someone", "someone") == "C:\\other" &&
              windows_home_of("other", "C:\\Profiles\\elsewhere", "someone") == std::nullopt &&
              windows_home_of("other", "C:\\Users\\someone", std::nullopt) == std::nullopt);

#ifdef _WIN32

namespace detail {
/// An environment variable as UTF-8; nullopt when it is not set.
[[nodiscard]] inline std::optional<std::string> environment(const wchar_t* name) {
    const wchar_t* value = ::_wgetenv(name);
    if (value == nullptr) return std::nullopt;
    const int wide = static_cast<int>(std::wcslen(value));
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, value, wide, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value, wide, out.data(), bytes, nullptr, nullptr);
    return out;
}
}  // namespace detail

/// `user`'s home ("" for this user's), as ntpath's expanduser finds it; nullopt when none is known.
[[nodiscard]] inline std::optional<std::string> home_of(std::string_view user) {
    std::string home;
    if (auto profile = detail::environment(L"USERPROFILE")) {
        home = std::move(*profile);
    } else if (auto path = detail::environment(L"HOMEPATH")) {
        home = windows_join(detail::environment(L"HOMEDRIVE").value_or(""), *path);
    } else {
        return std::nullopt;
    }
    const std::optional<std::string> current = detail::environment(L"USERNAME");
    return windows_home_of(user, home, current ? std::optional<std::string_view>(*current) : std::nullopt);
}

#else

namespace detail {
/// A user's home in the password database — `name`'s, or this process's user's when null.
[[nodiscard]] inline std::optional<std::string> password_home(const char* name) {
    const long hinted = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer(hinted > 0 ? static_cast<std::size_t>(hinted) : 16384);
    for (;;) {
        struct passwd entry {};
        struct passwd* found = nullptr;
        const int err = name != nullptr ? ::getpwnam_r(name, &entry, buffer.data(), buffer.size(), &found)
                                        : ::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found);
        if (err == ERANGE) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        if (err != 0 || found == nullptr || entry.pw_dir == nullptr) return std::nullopt;
        return std::string(entry.pw_dir);
    }
}
}  // namespace detail

/// `user`'s home ("" for this user's), as posixpath's expanduser finds it: $HOME for this user
/// when it is set (empty too), else the password database; nullopt when none is known.
[[nodiscard]] inline std::optional<std::string> home_of(std::string_view user) {
    std::optional<std::string> home;
    if (user.empty()) {
        const char* set = std::getenv("HOME");
        home = set != nullptr ? std::optional<std::string>(set) : detail::password_home(nullptr);
    } else {
        home = detail::password_home(std::string(user).c_str());
    }
    if (!home) return std::nullopt;
    return posix_home(std::move(*home));
}

#endif

}  // namespace pygim::platform
