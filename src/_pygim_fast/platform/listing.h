#pragma once
// platform/listing.h — a directory's entries, as each system lists them: POSIX's readdir, with
// each entry's kind from its d_type, and Windows' directory_iterator, which keeps each name's
// native UTF-16 spelling. Infrastructure behind the platform boundary
// (docs/design/platform_boundary.md, D77): the glob (pathlike/glob_walk.h) matches the entries and
// never asks which system listed them.
//
//     for (const dir_entry& e : list_directory(dir)) e.name(), e.kind(), e.under(dir)
//     if constexpr (finds_a_name_by_stat) entry_at(dir / "name")    // one stat for one named entry
//
// CORE layer: pybind-free.

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace pygim::platform {

namespace fs = std::filesystem;

/// What an entry of a directory is, as its listing says (a link is not followed): `file` a regular
/// file, `other` a FIFO, socket or device — or an entry whose kind could not be read.
enum class entry_kind { directory, file, link, other };

/// One entry of a directory listing: its name as UTF-8 text (what a pattern matches), its kind, and
/// on Windows its native name too — the UTF-16 one, which is what a path below it is joined from:
/// joined from the UTF-8 text, a name past ASCII came out in the ANSI code page (2026-10-29).
class dir_entry {
public:
    dir_entry(std::string name, entry_kind kind) noexcept : m_name(std::move(name)), m_kind(kind) {}
#ifdef _WIN32
    dir_entry(fs::path native, entry_kind kind) : m_name(utf8_of(native)), m_native(std::move(native)), m_kind(kind) {}
#endif
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] entry_kind kind() const noexcept { return m_kind; }
    /// The entry's path in `dir`.
    [[nodiscard]] fs::path under(const fs::path& dir) const {
#ifdef _WIN32
        return dir / m_native;
#else
        return dir / m_name;   // a POSIX name is its own native spelling
#endif
    }

private:
#ifdef _WIN32
    [[nodiscard]] static std::string utf8_of(const fs::path& native) {
        const std::u8string u = native.u8string();
        return std::string(u.begin(), u.end());
    }
#endif

    std::string m_name;
#ifdef _WIN32
    fs::path m_native;
#endif
    entry_kind m_kind;
};

#ifndef _WIN32
/// What an lstat's st_mode says an entry is.
[[nodiscard]] constexpr entry_kind kind_of_mode(mode_t mode) noexcept {
    return S_ISDIR(mode) ? entry_kind::directory : S_ISREG(mode) ? entry_kind::file : S_ISLNK(mode) ? entry_kind::link : entry_kind::other;
}
static_assert(kind_of_mode(S_IFDIR) == entry_kind::directory && kind_of_mode(S_IFREG) == entry_kind::file &&
              kind_of_mode(S_IFLNK) == entry_kind::link && kind_of_mode(S_IFSOCK) == entry_kind::other);

/// What a POSIX listing says an entry is (its d_type; a link is not followed). Some file systems
/// leave the kind out (DT_UNKNOWN): then lstat_mode() is asked — the entry's st_mode by lstat, or
/// nothing when lstat fails, and an entry it cannot tell is `other`, matched but never gone into.
template <class LstatMode>
[[nodiscard]] constexpr entry_kind kind_from_listing(unsigned char d_type, const LstatMode& lstat_mode) {
    if (d_type == DT_DIR) return entry_kind::directory;
    if (d_type == DT_LNK) return entry_kind::link;
    if (d_type == DT_REG) return entry_kind::file;
    if (d_type != DT_UNKNOWN) return entry_kind::other;
    const std::optional<mode_t> mode = lstat_mode();
    return mode ? kind_of_mode(*mode) : entry_kind::other;
}
static_assert([] {
    const auto never = []() -> std::optional<mode_t> { throw "a known kind asks no lstat"; };
    const auto says = [](mode_t m) { return [m] { return std::optional<mode_t>(m); }; };
    const auto fails = [] { return std::optional<mode_t>(); };
    return kind_from_listing(DT_DIR, never) == entry_kind::directory && kind_from_listing(DT_LNK, never) == entry_kind::link &&
           kind_from_listing(DT_REG, never) == entry_kind::file && kind_from_listing(DT_FIFO, never) == entry_kind::other &&
           kind_from_listing(DT_UNKNOWN, says(S_IFDIR)) == entry_kind::directory &&
           kind_from_listing(DT_UNKNOWN, says(S_IFLNK)) == entry_kind::link && kind_from_listing(DT_UNKNOWN, says(S_IFREG)) == entry_kind::file &&
           kind_from_listing(DT_UNKNOWN, says(S_IFIFO)) == entry_kind::other &&
           kind_from_listing(DT_UNKNOWN, fails) == entry_kind::other;
}());
#endif

/// The entries of a directory, without "." and ".."; none when it cannot be read.
[[nodiscard]] inline std::vector<dir_entry> list_directory(const fs::path& dir) {
    std::vector<dir_entry> out;
#ifdef _WIN32
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code kc;
        const entry_kind kind = it->is_symlink(kc) ? entry_kind::link : it->is_directory(kc) ? entry_kind::directory
                                : it->is_regular_file(kc) ? entry_kind::file : entry_kind::other;
        out.emplace_back(it->path().filename(), kind);
    }
#else
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return out;
    while (const dirent* e = ::readdir(d)) {
        const char* n = e->d_name;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0'))) continue;
        const entry_kind kind = kind_from_listing(e->d_type, [&]() -> std::optional<mode_t> {
            struct ::stat st {};
            if (::fstatat(::dirfd(d), n, &st, AT_SYMLINK_NOFOLLOW) != 0) return std::nullopt;
            return st.st_mode;
        });
        out.emplace_back(n, kind);
    }
    ::closedir(d);
#endif
    return out;
}

/// Whether one stat finds a named entry exactly as a listing's case-sensitive match would: where
/// the system's file systems tell names apart by case (Linux's). macOS's and Windows' default ones
/// fold case, and a stat of `src` there finds `Src`.
inline constexpr bool finds_a_name_by_stat =
#ifdef __linux__
    true;
#else
    false;
#endif

/// The entry at `path`, by one stat that does not follow a link: its kind, or nothing when there is
/// none or it cannot be read.
[[nodiscard]] inline std::optional<entry_kind> entry_at(const fs::path& path) noexcept {
#ifdef _WIN32
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(path, ec);
    if (ec || !fs::exists(st)) return std::nullopt;
    return fs::is_symlink(st) ? entry_kind::link : fs::is_directory(st) ? entry_kind::directory
           : fs::is_regular_file(st) ? entry_kind::file : entry_kind::other;
#else
    struct ::stat st {};
    if (::lstat(path.c_str(), &st) != 0) return std::nullopt;
    return kind_of_mode(st.st_mode);
#endif
}

}  // namespace pygim::platform
