#pragma once
// platform/posix/locks.h — the calls a POSIX file lock makes, one thin wrapper each: open, flock,
// close. Only platform/locks.h includes this, and only where the system is not Windows: nothing
// here asks which system it runs on (D106).
//
// CORE layer: pybind-free.

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>

namespace pygim::platform {

struct posix_lock_calls {
    /// The lock's file, opened for reading and writing and made if it is not there; -1 when it cannot be.
    [[nodiscard]] static int open(const std::filesystem::path& p) noexcept { return ::open(p.c_str(), O_RDWR | O_CREAT, 0644); }
    /// The exclusive lock, waited for; an interrupted wait waits again. False when it cannot be taken.
    [[nodiscard]] static bool lock(int fd) noexcept {
        while (::flock(fd, LOCK_EX) != 0)
            if (errno != EINTR) return false;
        return true;
    }
    static void unlock(int fd) noexcept { ::flock(fd, LOCK_UN); }
    static void close(int fd) noexcept { ::close(fd); }
};

}  // namespace pygim::platform
