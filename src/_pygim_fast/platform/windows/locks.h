#pragma once
// platform/windows/locks.h — the calls a Windows file lock makes, one thin wrapper each: _wsopen_s,
// LockFileEx and UnlockFileEx through the descriptor's own handle, _close. Only platform/locks.h
// includes this, and only on Windows: nothing here asks which system it runs on (D106).
//
// CORE layer: pybind-free.

#include "../windows_headers.h"

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <filesystem>

namespace pygim::platform {

struct win32_lock_calls {
    /// The lock's file, opened for reading and writing, shared, and made if it is not there; -1 when it
    /// cannot be — as CreateFileW with OPEN_ALWAYS and FILE_SHARE_READ | FILE_SHARE_WRITE opened it.
    [[nodiscard]] static int open(const std::filesystem::path& p) noexcept {
        int fd = -1;
        return ::_wsopen_s(&fd, p.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE) == 0 ? fd : -1;
    }
    /// The exclusive lock on the file's first byte, waited for. False when it cannot be taken.
    [[nodiscard]] static bool lock(int fd) noexcept {
        OVERLAPPED ov{};
        return ::LockFileEx(handle(fd), LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov) != 0;
    }
    static void unlock(int fd) noexcept {
        OVERLAPPED ov{};
        ::UnlockFileEx(handle(fd), 0, 1, 0, &ov);
    }
    static void close(int fd) noexcept { ::_close(fd); }

private:
    [[nodiscard]] static HANDLE handle(int fd) noexcept { return reinterpret_cast<HANDLE>(::_get_osfhandle(fd)); }
};

}  // namespace pygim::platform
