#pragma once
// platform/reads.h — reading one file whole, as each system does it. Infrastructure behind the
// platform boundary (docs/design/platform_boundary.md, D77): the readers give pathlike's values
// (pathlike/read_result.h) and meet its file_reads concept; native_reads is the running system's.
//
// On POSIX a read is one open, one fstat of the open descriptor and the reads: never a stat by path
// first, so nothing can change between a check and the read. A device is opened (non-blocking, so a
// FIFO cannot stall the read) before fstat says it is no file: telling it apart first cost a read
// of small files 37-65% (D64, docs/design/pathset_storage.md). The buffer is sized from st_size and
// not zero-filled, and a regular file of known size is done when a read comes back short — no extra
// read() to see its end (/proc files report st_size 0 and are read to the end): 7% and 6% off a
// warm read of many files (2026-10-25). On Windows, which has no FIFOs to stall a read, the status
// comes first and the bytes are copied into the arena.
//
// CORE layer: pybind-free.

#include <cerrno>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../pathlike/read_result.h"

namespace pygim::platform {

namespace fs = std::filesystem;
using pathlike::file;
using pathlike::file_error;
using pathlike::file_reads;
using pathlike::not_a_file;
using pathlike::os_path;
using pathlike::read_failure;
using pathlike::read_result;

#ifndef _WIN32

/// What fstat says of an open file: whether it is a regular file, and how big it says it is.
class open_status {
public:
    explicit open_status(const struct ::stat& st) noexcept : m_mode(st.st_mode), m_size(st.st_size) {}

    [[nodiscard]] bool is_regular() const noexcept { return S_ISREG(m_mode); }
    /// Why it is no file: a directory, or another kind of node.
    [[nodiscard]] not_a_file why_not() const noexcept { return not_a_file(S_ISDIR(m_mode) ? EISDIR : ENODEV); }
    /// Its length as fstat gives it: exact for a regular file, 0 for a /proc file whatever it holds.
    [[nodiscard]] std::size_t size() const noexcept { return m_size > 0 ? static_cast<std::size_t>(m_size) : 0; }

private:
    mode_t m_mode;
    off_t m_size;
};

/// One open file descriptor, closed when it goes.
class descriptor {
public:
    /// `path` opened for reading — non-blocking, so a FIFO cannot stall; never as the controlling
    /// terminal, should it be a tty — or the errno of the failure. A device is opened before it is
    /// told from a file (one open and one fstat a file, no stat by path first): its driver sees the
    /// open (D64).
    [[nodiscard]] static std::expected<descriptor, int> open_for_reading(os_path path) noexcept {
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) return std::unexpected(errno);
        return descriptor(fd);
    }

    descriptor(descriptor&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {}
    descriptor(const descriptor&) = delete;
    descriptor& operator=(const descriptor&) = delete;
    descriptor& operator=(descriptor&&) = delete;
    ~descriptor() {
        if (m_fd >= 0) ::close(m_fd);
    }

    /// What is open, from fstat: no second lookup by path.
    [[nodiscard]] std::expected<open_status, int> status() const noexcept {
        struct ::stat st {};
        if (::fstat(m_fd, &st) != 0) return std::unexpected(errno);
        return open_status(st);
    }

    /// Every byte to the end of a regular file fstat says is `size` long (0: unknown, read until
    /// read() says the end), in `arena` or on the heap. The buffer is one byte over, so a file of that
    /// size ends in one read.
    [[nodiscard]] std::expected<arena_string, int> read_all(std::size_t size, chunk_arena* arena) const {
        arena_string bytes{arena_allocator<char>(arena)};
        // A known size and one byte more, so the read that returns 0 says it ended; an unknown one (a
        // /proc file: fstat says 0) a page to start, where a byte took a dozen reads to grow to 4 KB.
        constexpr std::size_t unknown_start = 4096;
        bytes.resize_and_overwrite(size != 0 ? size + 1 : unknown_start, [](char*, std::size_t n) noexcept { return n; });   // filled by read(), not zeroed
        std::size_t have = 0;
        for (;;) {
            if (have == bytes.size()) bytes.resize_and_overwrite(bytes.size() * 2, [](char*, std::size_t n) noexcept { return n; });
            const std::size_t room = bytes.size() - have;
            const ssize_t got = ::read(m_fd, bytes.data() + have, room);
            if (got > 0) {
                have += static_cast<std::size_t>(got);
                if (size != 0 && have == size && static_cast<std::size_t>(got) < room) break;   // short at its known length: its end
            } else if (got == 0) {
                break;
            } else if (errno != EINTR) {
                return std::unexpected(errno);
            }
        }
        bytes.resize(have);
        return bytes;
    }

private:
    explicit descriptor(int fd) noexcept : m_fd(fd) {}

    int m_fd;
};

/// Reads one file through the POSIX calls: open, fstat on the descriptor, read to the end; its
/// bytes in `arena`, or on the heap without one.
class posix_reads {
public:
    [[nodiscard]] read_result read(os_path path, chunk_arena* arena = nullptr) const {
        auto opened = descriptor::open_for_reading(path);
        if (!opened) return refused(path, opened.error());
        const auto status = opened->status();
        if (!status) return read_failure(status.error());
        if (!status->is_regular()) return status->why_not();
        auto bytes = opened->read_all(status->size(), arena);
        if (!bytes) return read_failure(bytes.error());
        return std::move(*bytes);
    }

private:
    // The open failed. Nothing there, or a link loop, is no file (pathlib's is_file() says False);
    // otherwise one stat by path — on this cold path only — tells a regular file the OS refused
    // (raised) from a node a read of many leaves out.
    [[nodiscard]] static read_result refused(os_path path, int err) noexcept {
        if (err == ENOENT || err == ENOTDIR || err == ELOOP) return not_a_file(err);
        struct ::stat st {};
        if (::stat(path.c_str(), &st) == 0 && !S_ISREG(st.st_mode)) return open_status(st).why_not();
        return read_failure(err);
    }
};
static_assert(file_reads<posix_reads>);

using native_reads = posix_reads;

#else

/// Reads one file on Windows, which has no FIFOs to stall a read: the status, then read_bytes(),
/// copied into `arena` (or onto the heap) — a copy the POSIX reads do not make.
class windows_reads {
public:
    [[nodiscard]] read_result read(os_path path, chunk_arena* arena = nullptr) const {
        const file f(std::string(path.text()));
        std::error_code ec;
        const fs::file_status status = fs::status(f.to_fs(), ec);
        if (ec) {   // as POSIX's open: missing is left out, anything else the OS refused is raised
            const std::error_condition why = ec.default_error_condition();
            if (why == std::errc::no_such_file_or_directory || why == std::errc::not_a_directory || why == std::errc::too_many_symbolic_link_levels) {
                return not_a_file(why.value());
            }
            return read_failure(why.category() == std::generic_category() ? why.value() : EIO);
        }
        if (!fs::exists(status)) return not_a_file(ENOENT);
        if (!fs::is_regular_file(status)) return not_a_file(fs::is_directory(status) ? EISDIR : ENODEV);
        try {
            const std::string bytes = f.read_bytes();
            return arena_string(bytes.data(), bytes.size(), arena_allocator<char>(arena));
        } catch (const file_error& e) {
            return read_failure(e.code().value());
        }
    }
};
static_assert(file_reads<windows_reads>);

using native_reads = windows_reads;

#endif

}  // namespace pygim::platform
