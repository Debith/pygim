#pragma once
// pathlike/file_reads.h — reading one file whole, as a value: its bytes, or why there are none.
//
//     read_result r = native_reads{}.read(path);          // a std::string, or a path_list's entry
//     std::get<arena_string>(r)         -> the bytes        not_a_file / read_failure -> why not
//     native_reads{}.read(path, &arena)                   // the bytes in a chunk_arena's chunks
//
// A read takes the path's text as the OS takes it (os_path): open() is given it as it is, with no
// file parsed first and rendered again. Many paths are one path_list: one text, a NUL after each.
//
// What one file gives (read_result): its bytes; not_a_file — missing, a link loop, a directory, a
// FIFO or a device, which a read of many files leaves out, as pathlib's is_file() says False for
// each; or read_failure — a regular file the OS would not read, raised as pathlib raises it. On
// POSIX that is one open, one fstat of the open descriptor and the reads: never a stat by path
// first, so nothing can change between a check and the read. A device is opened (non-blocking, so a
// FIFO cannot stall the read) before fstat says it is no file: telling it apart first cost a read
// of small files 37-65% (D64, docs/design/pathset_storage.md).
//
// The buffer is sized from st_size and not zero-filled, and a regular file of known size is done
// when a read comes back short — no extra read() to see its end (/proc files report st_size 0 and
// are read to the end): 7% and 6% off a warm read of many files (2026-10-25). It is an arena_string:
// on the heap, or in the arena the caller names — a corpus reads its files into its workers' chunks
// (D69), where a std::string would grow the C library's per-thread heaps under them.
//
// CORE layer: pybind-free.

#include <cerrno>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../utils/chunk_arena.h"
#include "core.h"

namespace pygim::pathlike {

/// A path's text as the OS takes it, kept with a NUL after it — by a std::string or a path_list —
/// so open() is given it as it is. A view: what it was made from must outlive it.
class os_path {
public:
    constexpr os_path(const std::string& path) noexcept : m_text(path) {}   // implicit by design: read(path)
    os_path(std::string&&) = delete;                                         // a temporary's text would go first

    [[nodiscard]] constexpr const char* c_str() const noexcept { return m_text.data(); }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return m_text; }

private:
    friend class path_list;
    constexpr explicit os_path(std::string_view followed_by_nul) noexcept : m_text(followed_by_nul) {}

    std::string_view m_text;   // m_text.data()[m_text.size()] is '\0'
};
static_assert([] {
    const std::string held = "a.py";
    const os_path path(held);
    return path.text() == "a.py" && path.c_str()[4] == '\0';
}() && !std::is_constructible_v<os_path, std::string> && std::is_constructible_v<os_path, const std::string&>);

/// Paths kept as one text, a NUL after each: what a read of many files owns — two allocations for
/// the lot, where a file per path held a uri with a string per segment.
///
///     path_list l;  l.push_back("a.py");  l.push_back("b/c.py");     l.at(1).text() -> "b/c.py"
class path_list {
public:
    constexpr void reserve(std::size_t paths) { m_starts.reserve(paths); }
    constexpr void push_back(std::string_view path) {
        m_starts.push_back(m_text.size());
        m_text.append(path);
        m_text.push_back('\0');
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_starts.size(); }
    /// Path i; std::out_of_range past the end.
    [[nodiscard]] constexpr os_path at(std::size_t i) const {
        if (i >= size()) throw std::out_of_range("path_list::at");
        const std::size_t nul = (i + 1 < size() ? m_starts[i + 1] : m_text.size()) - 1;
        return os_path(std::string_view(m_text).substr(m_starts[i], nul - m_starts[i]));
    }

private:
    std::string m_text;                  // every path, each followed by its NUL
    std::vector<std::size_t> m_starts;   // where each path's text starts in it
};
static_assert([] {
    path_list l;
    l.push_back("a.py");
    l.push_back("");
    l.push_back("b/c.py");
    return l.size() == 3 && l.at(0).text() == "a.py" && l.at(1).text().empty() && l.at(2).text() == "b/c.py" && l.at(2).c_str()[6] == '\0';
}());

/// A member that is no regular file: missing (ENOENT, ENOTDIR), a link that loops (ELOOP), a
/// directory (EISDIR), or another kind of node such as a FIFO or a device (ENODEV).
class not_a_file {
public:
    constexpr explicit not_a_file(int why) noexcept : m_why(why) {}
    [[nodiscard]] constexpr int why() const noexcept { return m_why; }
    /// Why, as a reader is told: the OS's words for a missing path or a directory ("No such file or
    /// directory", "Is a directory"), and "not a regular file" for a FIFO or a device, where the
    /// OS's ENODEV ("No such device") would mislead.
    [[nodiscard]] std::string what() const { return m_why == ENODEV ? std::string("not a regular file") : std::generic_category().message(m_why); }

private:
    int m_why;
};
static_assert(not_a_file(EISDIR).why() == EISDIR);

/// What a run of many files says of a member it left out: "<path>: <why>", as an OSError names it.
template <class Reason>
[[nodiscard]] std::string left_out(os_path path, const Reason& why) {
    return std::string(path.text()) + ": " + why.what();
}

/// A regular file the OS would not read (EACCES, EIO, ...): raised as the OSError pathlib raises.
class read_failure {
public:
    constexpr explicit read_failure(int error) noexcept : m_error(error) {}
    [[nodiscard]] constexpr int error() const noexcept { return m_error; }
    /// Why, in the OS's words: "Permission denied".
    [[nodiscard]] std::string what() const { return std::generic_category().message(m_error); }
    [[noreturn]] void raise(os_path path) const { throw file_error(m_error, std::string(path.text()), "cannot read file"); }

private:
    int m_error;
};
static_assert(read_failure(EACCES).error() == EACCES);

/// What reading one file gave: its bytes (in the arena the read was given, else on the heap), or
/// why there are none.
using read_result = std::variant<arena_string, not_a_file, read_failure>;

/// What a read weighs while it waits to be handed out: its bytes.
class read_weight {
public:
    [[nodiscard]] constexpr std::size_t operator()(const read_result& r) const noexcept {
        const arena_string* bytes = std::get_if<arena_string>(&r);
        return bytes ? bytes->size() : 0;
    }
};
static_assert([] {
    const read_result bytes(arena_string("abc"));
    const read_result none(not_a_file(ENOENT));
    return read_weight{}(bytes) == 3 && read_weight{}(none) == 0;
}());

/// A read as a reader of many files takes it: the bytes; nothing for a member that is no file
/// (left out); a read the OS refused thrown as its file_error, naming `path` — which ends the reading.
[[nodiscard]] inline std::optional<arena_string> file_bytes(read_result&& got, os_path path) {
    if (arena_string* bytes = std::get_if<arena_string>(&got)) return std::move(*bytes);
    if (const read_failure* refused = std::get_if<read_failure>(&got)) refused->raise(path);
    return std::nullopt;
}

/// How one file is read: anything whose read(path) gives a read_result.
template <class R>
concept file_reads = requires(const R& reads, os_path path) {
    { reads.read(path) } -> std::same_as<read_result>;
};

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

}  // namespace pygim::pathlike
