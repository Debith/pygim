#pragma once
// platform/errors.h — the translation of a system's errors into the words and errors pygim gives:
// the ACL between an errno and what a reader is told (docs/design/platform_boundary.md, D77).
//
//     errno_words(ENOENT)                    -> "No such file or directory", as os.strerror says
//     open_error_for<family::windows>(EISDIR) -> EACCES: open() of a folder there is PermissionError
//     os_error_of(code)                       -> {errno, or the Windows error Python maps itself}
//
// CORE layer: pybind-free.

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

/// What a system's error code tells Python's OSError: an errno, or on Windows the system's own
/// error, which OSError(errno, words, filename, winerror) maps to its errno and subclass by Python's
/// own table, as os's calls are raised there. MSVC's portable condition maps some another way:
/// ERROR_DIRECTORY (267, iterdir of a file) is EINVAL there and ENOTDIR (NotADirectoryError) in
/// Python (CI, 2026-11-01).
class os_error {
public:
    /// From a std::error_code: the system's on a system family F whose codes are not errno's, else its errno.
    template <family F = native>
    [[nodiscard]] static os_error of(const std::error_code& code) {
        if (F == family::windows && code.category() == std::system_category()) return os_error(0, code.value(), windows_words(code.message()));
        const std::error_condition condition = code.default_error_condition();
        const int err = condition.category() == std::generic_category() ? condition.value() : EIO;
        return os_error(err, std::nullopt, errno_words(err));
    }

    [[nodiscard]] int errno_value() const noexcept { return m_errno; }
    /// The system's own error, where Python is to map it; none where the errno is the answer.
    [[nodiscard]] std::optional<int> winerror() const noexcept { return m_winerror; }
    [[nodiscard]] const std::string& words() const noexcept { return m_words; }

    /// A Windows message as Python words it: its trailing line break and full stop dropped.
    [[nodiscard]] static constexpr std::string windows_words(std::string message) {
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ' || message.back() == '.')) {
            message.pop_back();
        }
        return message;
    }

private:
    int m_errno;
    std::optional<int> m_winerror;
    std::string m_words;

    os_error(int err, std::optional<int> winerror, std::string words) : m_errno(err), m_winerror(winerror), m_words(std::move(words)) {}
};
static_assert(os_error::windows_words("The directory name is invalid.\r\n") == "The directory name is invalid" &&
              os_error::windows_words("") == "" && os_error::windows_words("Access is denied.") == "Access is denied");

}  // namespace pygim::platform
