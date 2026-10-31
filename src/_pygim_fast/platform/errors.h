#pragma once
// platform/errors.h — the translation of a system's errors into the words and errors pygim gives:
// the ACL between an errno and what a reader is told (docs/design/platform_boundary.md, D77).
//
//     errno_words(ENOENT)                    -> "No such file or directory", as os.strerror says
//     open_error_for<family::windows>(EISDIR) -> EACCES: open() of a folder there is PermissionError
//
// CORE layer: pybind-free.

#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>

#include "native.h"

namespace pygim::platform {

/// The system's words for an errno, as Python's os.strerror gives them: the C runtime's strerror.
/// std::generic_category() gives the same words with glibc and macOS's libc, and lowercases them
/// with MSVC ("no such file or directory").
[[nodiscard]] inline std::string errno_words(int err) {
#ifdef _WIN32
    char words[128];
    if (strerror_s(words, sizeof words, err) == 0) return words;
#endif
    return std::generic_category().message(err);
}

/// The errno open() fails with on a path that is no regular file, under family F's rules, for why
/// it is none (ENOENT, ELOOP, EISDIR, ENODEV): why itself, but for a folder on Windows, which open()
/// refuses as PermissionError (EACCES).
template <family F = native>
[[nodiscard]] constexpr int open_error_for(int why) noexcept {
    return F == family::windows && why == EISDIR ? EACCES : why;
}
static_assert(open_error_for<family::windows>(EISDIR) == EACCES && open_error_for<family::posix>(EISDIR) == EISDIR &&
              open_error_for<family::windows>(ENOENT) == ENOENT && open_error_for<family::posix>(ELOOP) == ELOOP);

}  // namespace pygim::platform
