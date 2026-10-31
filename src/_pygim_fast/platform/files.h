#pragma once
// platform/files.h — the file system's calls pathlike makes, one spelling per system: a path's
// native text, exclusive create, write, force to the disk, close, unlink. Infrastructure behind the
// platform boundary (docs/design/platform_boundary.md, D77): pathlike's files and atomic writes
// (pathlike/core.h) call these and never ask which system they run on. Each call returns what the
// system's returns and leaves errno as it left it.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>        // _wunlink, _wsopen_s, _write, _commit, _close
#include <share.h>
#else
#include <unistd.h>    // unlink, write, fsync, close
#endif

namespace pygim::platform {

namespace fs = std::filesystem;

// The OS boundary: the internal text form is the NATIVE byte string on POSIX
// (so non-UTF-8 file names round-trip) and UTF-8 on Windows (lossless from the
// wide native form, and what Python's str carries). fs::path::string() would
// narrow through the ACP on Windows and can mangle non-ASCII paths.
[[nodiscard]] inline std::string text_from_fs(const fs::path& p) {
#ifdef _WIN32
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.native();
#endif
}

[[nodiscard]] inline fs::path fs_from_text(std::string_view text) {
#ifdef _WIN32
    return fs::path(std::u8string(text.begin(), text.end()));
#else
    return fs::path(std::string(text));
#endif
}

// The operating system calls an atomic write needs, one spelling per platform. Each returns what the
// call returns and leaves errno as the call left it.
#ifdef _WIN32
[[nodiscard]] inline int open_exclusive(const fs::path& p) {
    int fd = -1;
    const errno_t err = ::_wsopen_s(&fd, p.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                                    _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (err != 0) errno = err;
    return err == 0 ? fd : -1;
}
[[nodiscard]] inline long long write_some(int fd, const char* data, std::size_t size) {
    return ::_write(fd, data, static_cast<unsigned>(std::min<std::size_t>(size, INT_MAX)));
}
inline int force_fd(int fd) { return ::_commit(fd); }
inline int close_fd(int fd) { return ::_close(fd); }
inline int force_folder(const fs::path&) { return 0; }   // Windows has no folder to force; see pathlike_writes.md, Open
#else
[[nodiscard]] inline int open_exclusive(const fs::path& p) {
    return ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
}
[[nodiscard]] inline long long write_some(int fd, const char* data, std::size_t size) { return ::write(fd, data, size); }
inline int force_fd(int fd) {
#ifdef __APPLE__
    if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;   // macOS's fsync stops at the drive's cache
#endif
    return ::fsync(fd);
}
inline int close_fd(int fd) { return ::close(fd); }
inline int force_folder(const fs::path& folder) {
    const int fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    const int forced = force_fd(fd);
    const int err = errno;
    ::close(fd);
    errno = err;
    return forced;
}
#endif

/// Removes the file or symbolic link at `p` (a link goes, not what it points to): 0, or -1 with
/// errno as the system's unlink left it.
[[nodiscard]] inline int unlink_file(const fs::path& p) {
#ifdef _WIN32
    return ::_wunlink(p.c_str());
#else
    return ::unlink(p.c_str());
#endif
}

}  // namespace pygim::platform
