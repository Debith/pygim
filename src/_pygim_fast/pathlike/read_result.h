#pragma once
// pathlike/read_result.h — what reading one file whole gives, as a value — its bytes, or why there
// are none — and the concept a reader of files meets. The readers are the system's
// (platform/reads.h); pathlike/file_reads.h names the running system's, native_reads.
//
// A read takes the path's text as the OS takes it (os_path): open() is given it as it is, with no
// file parsed first and rendered again. Many paths are one path_list: one text, a NUL after each.
//
// What one file gives (read_result): its bytes, an arena_string — on the heap, or in the arena the
// caller names (a corpus reads its files into its workers' chunks, D69); not_a_file — missing, a
// link loop, a directory, a FIFO or a device, which a read of many files leaves out, as pathlib's
// is_file() says False for each; or read_failure — a regular file the OS would not read, raised as
// pathlib raises it.
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

#include "../platform/errors.h"
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
    [[nodiscard]] std::string what() const { return m_why == ENODEV ? std::string("not a regular file") : platform::errno_words(m_why); }
    /// The errno open() fails with on it, for a caller that raises what open() would: why it is no
    /// file — but a folder on Windows, which open() refuses as PermissionError (EACCES).
    [[nodiscard]] constexpr int open_error() const noexcept { return platform::open_error_for(m_why); }

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
    [[nodiscard]] std::string what() const { return platform::errno_words(m_error); }
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


}  // namespace pygim::pathlike
