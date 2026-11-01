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
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>        // _wunlink, _wsopen_s, _write, _commit, _close
#include <share.h>
#else
#include <unistd.h>    // unlink, write, fsync, close
#endif

#include "native.h"

namespace pygim::platform {

namespace fs = std::filesystem;

/// What a path's internal text is: the system's own bytes on POSIX (so a name that is not UTF-8
/// round-trips), UTF-8 on Windows (lossless from the wide native form, and what Python's str
/// carries). The Python boundary encodes a str by it (pathlike/adapter/path_text.h).
inline constexpr bool path_text_is_utf8 = native == family::windows;

/// What text mode writes for "\n" when no newline is asked: os.linesep.
inline constexpr std::string_view line_separator = native == family::windows ? "\r\n" : "\n";
static_assert(line_separator == "\n" || line_separator == "\r\n");

// The OS boundary: the internal text form, as path_text_is_utf8 says. fs::path::string() would
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
[[nodiscard]] inline int open_reading(const fs::path& p) {
    int fd = -1;
    const errno_t err = ::_wsopen_s(&fd, p.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT, _SH_DENYNO, 0);
    if (err != 0) errno = err;
    return err == 0 ? fd : -1;
}
[[nodiscard]] inline long long read_some(int fd, char* data, std::size_t size) {
    return ::_read(fd, data, static_cast<unsigned>(std::min<std::size_t>(size, INT_MAX)));
}
[[nodiscard]] inline std::size_t told_size(int fd) {
    struct ::_stat64 st {};
    return ::_fstat64(fd, &st) == 0 && st.st_size > 0 ? static_cast<std::size_t>(st.st_size) : 0;
}
[[nodiscard]] inline int open_appending(const fs::path& p) {
    int fd = -1;
    const errno_t err = ::_wsopen_s(&fd, p.c_str(), _O_RDWR | _O_APPEND | _O_CREAT | _O_BINARY | _O_NOINHERIT, _SH_DENYNO,
                                    _S_IREAD | _S_IWRITE);
    if (err != 0) errno = err;
    return err == 0 ? fd : -1;
}
[[nodiscard]] inline int last_byte(int fd, std::size_t size) {
    unsigned char c = 0;
    return size != 0 && ::_lseeki64(fd, static_cast<__int64>(size - 1), SEEK_SET) >= 0 && ::_read(fd, &c, 1) == 1 ? c : -1;
}
inline int force_fd(int fd) { return ::_commit(fd); }
inline int close_fd(int fd) { return ::_close(fd); }
inline int force_folder(const fs::path&) { return 0; }   // Windows has no folder to force; see pathlike_writes.md, Open
#else
[[nodiscard]] inline int open_exclusive(const fs::path& p) {
    return ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
}
[[nodiscard]] inline long long write_some(int fd, const char* data, std::size_t size) { return ::write(fd, data, size); }
[[nodiscard]] inline int open_reading(const fs::path& p) { return ::open(p.c_str(), O_RDONLY | O_CLOEXEC); }
[[nodiscard]] inline long long read_some(int fd, char* data, std::size_t size) { return ::read(fd, data, size); }
[[nodiscard]] inline std::size_t told_size(int fd) {
    struct ::stat st {};
    return ::fstat(fd, &st) == 0 && st.st_size > 0 ? static_cast<std::size_t>(st.st_size) : 0;
}
[[nodiscard]] inline int open_appending(const fs::path& p) { return ::open(p.c_str(), O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0666); }
[[nodiscard]] inline int last_byte(int fd, std::size_t size) {
    unsigned char c = 0;
    return size != 0 && ::pread(fd, &c, 1, static_cast<off_t>(size - 1)) == 1 ? c : -1;
}
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

/// Reads every byte of the file at `p` into the room `grow` gives — grow(n) returns a buffer of
/// at least n bytes that keeps what it held — and returns how many, or the errno of the open or
/// the read that failed. Opened as open() opens a file for reading (a FIFO waits for its writer,
/// as pathlib's read_bytes does; a directory opens on POSIX and its read fails with EISDIR,
/// Windows refuses its open with EACCES). The room is sized from what fstat tells, one byte over:
/// a regular file ends at the read that comes back short at its size, a file that tells 0 (a
/// /proc one) is read until a read returns nothing. Found 2026-10-31: an ifstream copied through
/// an ostringstream and then into a bytes object read 50 MB in 124 ms where pathlib took 28.
template <class Grow>
[[nodiscard]] std::expected<std::size_t, int> read_whole_into(const fs::path& p, Grow&& grow) {
    const int fd = open_reading(p);
    if (fd < 0) return std::unexpected(errno);
    const struct closer {
        int fd;
        ~closer() { (void)close_fd(fd); }
    } close_at_end{fd};
    const std::size_t size = told_size(fd);
    std::size_t room = size != 0 ? size + 1 : 4096;
    char* data = grow(room);
    std::size_t have = 0;
    for (;;) {
        if (have == room) data = grow(room *= 2);
        const long long got = read_some(fd, data + have, room - have);
        if (got > 0) {
            have += static_cast<std::size_t>(got);
            if (size != 0 && have == size && have < room) break;   // short at its size: its end
        } else if (got == 0) {
            break;
        } else if (errno != EINTR) {
            return std::unexpected(errno);
        }
    }
    return have;
}

/// Appends `text` to the file at `p`, made when it is not there, as lines: a "\n" first when the
/// file's last byte is none (a last line written by hand), so a line is never joined to another.
/// 0, or the errno of what failed.
[[nodiscard]] inline int append_lines(const fs::path& p, std::string_view text) {
    const int fd = open_appending(p);
    if (fd < 0) return errno;
    std::string broken;
    std::string_view rest = text;
    if (const std::size_t size = told_size(fd); size != 0 && last_byte(fd, size) != '\n') {
        broken.reserve(text.size() + 1);
        broken += '\n';
        broken += text;
        rest = broken;
    }
    int failed = 0;
    while (!rest.empty()) {
        const long long put = write_some(fd, rest.data(), rest.size());
        if (put > 0) {
            rest.remove_prefix(static_cast<std::size_t>(put));
        } else if (put < 0 && errno == EINTR) {
            continue;
        } else {
            failed = put < 0 ? errno : EIO;
            break;
        }
    }
    if (close_fd(fd) != 0 && failed == 0) failed = errno;
    return failed;
}

/// A file's size in bytes, a link followed, by one rule on every system: a folder has none
/// (std::errc::is_a_directory, Python's IsADirectoryError). std::filesystem leaves a folder's size
/// to the implementation — libstdc++ and libc++ refuse it, MSVC's answers — so Windows asks first.
/// A path that is not there, or one the OS refuses, is the OS's error, naming it.
template <family F = native>
[[nodiscard]] std::uintmax_t file_size(const fs::path& p) {
    if constexpr (F == family::windows) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) throw fs::filesystem_error("cannot get the size of a folder", p, std::make_error_code(std::errc::is_a_directory));
    }
    return fs::file_size(p);
}

}  // namespace pygim::platform
