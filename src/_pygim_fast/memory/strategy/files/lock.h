// memory/strategy/files/lock.h — the commit lock and durable appends (03 §4).
//
// One operating-system lock on local/commit.lock serialises commits across
// every process sharing a clone; an in-process mutex does the same across
// threads, since flock-style locks do not. Appends are flushed to disk before
// they return, because the row is the commit point.
#pragma once

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace pygim::memory::strategy::files {

class file_lock {
public:
    explicit file_lock(const std::filesystem::path& p) {
        std::filesystem::create_directories(p.parent_path());
#ifdef _WIN32
        m_handle = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) throw std::runtime_error(p.string() + ": cannot open the commit lock");
#else
        m_fd = ::open(p.c_str(), O_RDWR | O_CREAT, 0644);
        if (m_fd < 0) throw std::runtime_error(p.string() + ": cannot open the commit lock");
#endif
    }
    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;
    ~file_lock() {
#ifdef _WIN32
        if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
#else
        if (m_fd >= 0) ::close(m_fd);
#endif
    }

    /// Held until destroyed; movable, so a store can return it.
    class guard {
    public:
        explicit guard(file_lock* l) : m_lock(l) {}
        guard(guard&& o) noexcept : m_lock(o.m_lock) { o.m_lock = nullptr; }
        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;
        guard& operator=(guard&&) = delete;
        ~guard() {
            if (m_lock) m_lock->release();
        }

    private:
        file_lock* m_lock;
    };

    [[nodiscard]] guard acquire() {
        m_mutex.lock();
#ifdef _WIN32
        OVERLAPPED ov{};
        if (!LockFileEx(m_handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov)) {
            m_mutex.unlock();
            throw std::runtime_error("cannot take the commit lock");
        }
#else
        while (::flock(m_fd, LOCK_EX) != 0) {
            if (errno != EINTR) {
                m_mutex.unlock();
                throw std::runtime_error("cannot take the commit lock");
            }
        }
#endif
        return guard(this);
    }

private:
    void release() {
#ifdef _WIN32
        OVERLAPPED ov{};
        UnlockFileEx(m_handle, 0, 1, 0, &ov);
#else
        ::flock(m_fd, LOCK_UN);
#endif
        m_mutex.unlock();
    }

#ifdef _WIN32
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_fd = -1;
#endif
    std::mutex m_mutex;
};

/// Appends `bytes` to `p` and flushes them to the disk before returning.
inline void append_durably(const std::filesystem::path& p, std::string_view bytes) {
    std::filesystem::create_directories(p.parent_path());
#ifdef _WIN32
    FILE* f = _wfopen(p.c_str(), L"ab");
#else
    FILE* f = std::fopen(p.c_str(), "ab");
#endif
    if (!f) throw std::runtime_error(p.string() + ": cannot append");
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() && std::fflush(f) == 0;
#ifdef _WIN32
    const bool synced = _commit(_fileno(f)) == 0;
#else
    const bool synced = ::fsync(fileno(f)) == 0;
#endif
    std::fclose(f);
    if (!ok || !synced) throw std::runtime_error(p.string() + ": append did not reach the disk");
}

/// Writes `bytes` to `p` through a temporary file and a rename, so a reader
/// sees the old file or the new one and never half of either.
inline void write_atomically(const std::filesystem::path& p, std::string_view bytes) {
    std::filesystem::create_directories(p.parent_path());
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error(tmp.string() + ": cannot write");
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error(tmp.string() + ": write failed");
    }
    std::filesystem::rename(tmp, p);
}

[[nodiscard]] inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error(p.string() + ": cannot read");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace pygim::memory::strategy::files
