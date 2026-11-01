#pragma once
// pathlike/core.h — the pybind-free heart of `pygim.path`.
//
// A `file` is a path that knows how to read and decode itself. Its VALUE is a
// `uri` (uri.h): a list of segments plus authority and root flag. A STRATEGY
// (posix_strategy / windows_strategy) says how native path text maps onto that
// value and back — separators, drives, UNC hosts, roots — the way pathlib's
// PurePosixPath and PureWindowsPath do. Both strategies are pure policy types, so
// the Windows rules are provable on any host (tests/static/*proofs.cpp).
//
// The path ALGEBRA (name, stem, suffix, parts, parent, join, ...) is constexpr
// and follows pathlib's rules; the FILESYSTEM half (exists, read_bytes, glob,
// ...) is runtime and goes through std::filesystem at the OS boundary. Which
// DECODER handles a path is still decided at compile time from the extension,
// and the set of decoders is open: every engine is one header in
// adapter/engines/ (registry.h explains the mechanism and the proofs).
//
// This header carries no pybind11 and no parser library, so it stays fast to
// compile and trivially testable; the engine glue that must speak Python lives
// in adapter/.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "../utils/chars.h"
#include "../utils/hash.h"
#include "../utils/string_list.h"
#include "../platform/files.h"
#include "../platform/home.h"
#include "glob_walk.h"
#include "path_rules.h"
#include "uri.h"


namespace pygim::pathlike {

namespace fs = std::filesystem;

// A file operation the operating system refused, as errno and the path it was about — what
// Python's OSError carries, so the adapter raises the subclass pathlib raises for the same call:
//   path("missing").read_bytes()  ->  file_error{ENOENT, "missing"}  ->  FileNotFoundError
//   path("a_dir").mkdir()         ->  file_error{EEXIST, "a_dir"}    ->  FileExistsError
// Derives from std::runtime_error (through std::system_error), so C++ that catches that keeps working.
struct file_error : std::system_error {
    std::string path;   // as fspath() spells it

    file_error(int err, std::string where, const std::string& what)
        : std::system_error(err, std::generic_category(), what + " '" + where + "'"), path(std::move(where)) {}
};

namespace detail {

// errno as the failed open left it. A library that failed without setting it gets the nearest
// answer the file system gives: nothing there is ENOENT, anything else EACCES.
[[nodiscard]] inline int open_errno(const fs::path& p) noexcept {
    const int err = errno;
    if (err != 0) return err;
    std::error_code ec;
    return fs::exists(p, ec) ? EACCES : ENOENT;
}

// Sixteen hex digits from the system's entropy: what tells one writer's temporary file from another's.
[[nodiscard]] inline std::string random_tag() {
    std::random_device entropy;
    const std::uint64_t bits = (std::uint64_t{entropy()} << 32) ^ std::uint64_t{entropy()};
    std::string out(16, '0');
    for (std::size_t i = 0; i < out.size(); ++i) out[out.size() - 1 - i] = "0123456789abcdef"[(bits >> (4 * i)) & 0xF];
    return out;
}

// The system's calls, from behind the platform boundary (platform/files.h).
using platform::close_fd;
using platform::force_fd;
using platform::force_folder;
using platform::fs_from_text;
using platform::open_exclusive;
using platform::text_from_fs;
using platform::write_some;

}  // namespace detail

/// Whether a write replaces the file in place (pathlib's write) or atomically (a temporary renamed
/// over it). Example: f.write_bytes("{}", write_mode::atomic).
enum class write_mode { in_place, atomic };

/// How an atomic write forces bytes to the disk: the operating system's own calls. A policy, so a
/// test can record the order of forcing instead (docs/design/pathlike_writes.md, step 4). A folder
/// on a file system that cannot force one (EINVAL, ENOTSUP) is let be: the rename has been made.
struct disk_sync {
    static void file(int fd, const std::string& where) {
        if (detail::force_fd(fd) != 0) throw file_error(errno, where, "cannot force to the disk");
    }
    static void folder(const fs::path& folder, const std::string& where) {
        if (detail::force_folder(folder) == 0 || errno == EINVAL || errno == ENOTSUP) return;
        throw file_error(errno, where, "cannot force its folder to the disk");
    }
};

template <class File, class Sync = disk_sync>
class basic_atomic_write;

// The pybind-free face of one engine: everything the registry, the proofs and
// `file` need to know. Every engine header declares one as `static constexpr
// engine_info info`; its address is the engine's identity at run time (static
// constexpr members are inline variables, so there is exactly one per program).
struct engine_info {
    std::string_view name;     // format name: engine="json"; the Python class is "<name>path"
    std::string_view label;    // LIBRARY label reported by .engine: "simdjson"
    std::string_view doc;      // one sentence, used by every derived docstring
    string_list exts;          // auto-dispatch extensions: lower-case, leading dot
    string_list aliases;       // extra engine= spellings: "yml", "ndjson", "tomlplusplus"

    // engine= selectors: the format name, the library label and the aliases.
    [[nodiscard]] constexpr bool selects(std::string_view n) const noexcept {
        return n == name || n == label || aliases.contains(n);
    }
    [[nodiscard]] constexpr bool owns(std::string_view ext) const noexcept { return exts.contains(ext); }
};

namespace detail {
// The value hash of basic_file and path_table: one definition, in utils/hash.h.
using pygim::hash::fnv1a;
using pygim::hash::fnv_basis;
using pygim::hash::fnv_prime;
using pygim::hash::mix_string;

}  // namespace detail

// A path that reads and decodes itself. Models os.PathLike (it exposes
// __fspath__ through the binding), so it drops straight into open(), Path(),
// etc. An engine may be pinned at construction (nullptr = auto by extension);
// derived paths (parent(), with_suffix(), ...) inherit the pin. Resolution
// itself lives in the registry (registry.h: engine_list::resolve).
template <class Strategy>
class basic_file {
public:
    using strategy = Strategy;

    constexpr basic_file() = default;   // "."

    // From a value already in the model (a path_table row, a parsed uri).
    constexpr explicit basic_file(uri value, const engine_info* pin = nullptr) : m_uri(std::move(value)), m_pin(pin) {}

    // From native path text — or a file:// URI (RFC 8089, decoded as
    // pathlib.Path.from_uri does). Any other "scheme://" text is rejected.
    constexpr explicit basic_file(std::string_view text, const engine_info* pin = nullptr) : m_pin(pin) {
        assign_text(m_uri, text);
    }

    // ── construction rules (constexpr, so provable) ────────────────────────
    [[nodiscard]] static constexpr bool is_file_scheme(std::string_view sch) noexcept {
        return chars::lower(sch) == "file";
    }

    // Parse path text or a file URI (RFC 8089) into `u`, in ONE pass: "" on
    // success, otherwise the reason `text` is rejected (`u` is then unspecified).
    // The single source of the acceptance rules — problem() and assign_text()
    // are views of it.
    [[nodiscard]] static constexpr std::string parse_or_problem(uri& u, std::string_view text) {
        const std::string_view sch = uri::scheme_of(text);
        if (sch.empty()) {
            Strategy::parse_into(u, text);
            return "";
        }
        if (is_file_scheme(sch)) {
            const uri parsed = uri::parse(text);
            if (!Strategy::remote_hosts) {
                if (const std::string_view host = detail::remote_host(parsed); !host.empty()) {
                    return "file URI names a remote host '" + std::string(host) + "' in '" + std::string(text) +
                           "' (only localhost is supported on POSIX, as pathlib.Path.from_uri)";
                }
            }
            Strategy::parse_into(u, Strategy::from_uri_text(parsed));
            if (!Strategy::is_absolute(u)) return "URI is not absolute: '" + std::string(text) + "'";
            return "";
        }
        const std::string_view rest = text.substr(sch.size() + 1);
        const bool drive_letter = Strategy::name == "windows" && sch.size() == 1;
        if (rest.starts_with("//") && !drive_letter) {
            return "unsupported URI scheme '" + std::string(sch) + "' in '" + std::string(text) +
                   "' (only file:// URIs are accepted)";
        }
        Strategy::parse_into(u, text);   // "note:x.yaml": a colon alone is not a URL
        return "";
    }

    // "" when `text` is a plain path or a valid file URI; otherwise the reason it is rejected.
    [[nodiscard]] static constexpr std::string problem(std::string_view text) {
        uri scratch;
        return parse_or_problem(scratch, text);
    }

    // Assign path text or a file URI to `u`; throws std::invalid_argument (ValueError) when rejected.
    static constexpr void assign_text(uri& u, std::string_view text) {
        const std::string why = parse_or_problem(u, text);
        if (!why.empty()) throw std::invalid_argument(why);
    }

    // ── value ──────────────────────────────────────────────────────────────
    [[nodiscard]] constexpr const uri& value() const noexcept { return m_uri; }
    [[nodiscard]] constexpr const engine_info* pinned() const noexcept { return m_pin; }

    // os.PathLike: the native path text, in pathlib's normalised spelling.
    [[nodiscard]] constexpr std::string fspath() const { return Strategy::render(m_uri); }

    // Value equality (the pin does not take part); ordering is element-wise.
    [[nodiscard]] constexpr bool operator==(const basic_file& o) const noexcept { return m_uri == o.m_uri; }

    // A hash of exactly what operator== compares (FNV-1a over the value), so
    // equal files hash equal without rendering the path text.
    [[nodiscard]] constexpr std::uint64_t hash_value() const noexcept {
        std::uint64_t h = detail::fnv_basis;
        if (m_uri.has_authority) h = detail::mix_string(h, m_uri.authority);
        h ^= (m_uri.has_authority ? 2u : 0u) | (m_uri.absolute ? 1u : 0u);
        h *= detail::fnv_prime;
        for (const std::string& s : m_uri.segments) h = detail::mix_string(h, s);
        return h;
    }
    [[nodiscard]] constexpr bool operator<(const basic_file& o) const noexcept {
        return std::tie(m_uri.has_authority, m_uri.authority, m_uri.absolute, m_uri.segments) <
               std::tie(o.m_uri.has_authority, o.m_uri.authority, o.m_uri.absolute, o.m_uri.segments);
    }

    // ── name components (pathlib parity: case preserved) ──────────────────
    [[nodiscard]] constexpr bool has_name() const noexcept { return m_uri.segments.size() > anchor_count(); }
    [[nodiscard]] constexpr std::string name() const { return has_name() ? m_uri.segments.back() : std::string(); }

    [[nodiscard]] constexpr std::string stem() const {
        const std::string n = name();
        return std::string(detail::stem_of(n));
    }

    // Final extension including the dot (".gz"); "" if none. A leading-dot
    // name (".bashrc") and a trailing dot ("a.") have no suffix, as in pathlib.
    [[nodiscard]] constexpr std::string suffix() const {
        const std::string n = name();
        return std::string(detail::suffix_of(n));
    }

    // Every extension of the final component: "a.tar.gz" -> [".tar", ".gz"].
    [[nodiscard]] constexpr std::vector<std::string> suffixes() const {
        const std::string n = name();
        std::vector<std::string> out;
        if (n.empty() || n.back() == '.') return out;
        const std::size_t start = n.find_first_not_of('.');
        if (start == std::string::npos) return out;
        std::size_t dot = n.find('.', start);
        while (dot != std::string::npos) {
            const std::size_t next = n.find('.', dot + 1);
            out.push_back(n.substr(dot, next == std::string::npos ? next : next - dot));
            dot = next;
        }
        return out;
    }

    // The lower-cased extension the registry dispatches on (".yaml").
    [[nodiscard]] constexpr std::string ext_key() const { return chars::lower(suffix()); }

    // pathlib's parts: the anchor ("/", "C:\", "\\server\share\"), then the components.
    [[nodiscard]] constexpr std::vector<std::string> parts() const {
        std::vector<std::string> out;
        if (const std::string a = Strategy::anchor(m_uri); !a.empty()) out.push_back(a);
        for (std::size_t i = anchor_count(); i < m_uri.segments.size(); ++i) out.push_back(m_uri.segments[i]);
        return out;
    }

    [[nodiscard]] constexpr bool is_absolute() const noexcept { return Strategy::is_absolute(m_uri); }

    // ── URI form ───────────────────────────────────────────────────────────
    // An absolute path renders per RFC 3986/8089 (file:///a%20b, file://host/share/x).
    // A relative path keeps the legacy "file://<path>" spelling (it has no RFC form).
    [[nodiscard]] constexpr std::string as_uri() const {
        if (is_absolute()) {
            uri u = m_uri;
            Strategy::make_file_uri(u);
            return u.render();
        }
        std::string out = "file://";
        for (std::size_t i = 0; i < m_uri.segments.size(); ++i) {
            if (i) out += '/';
            out += uri::percent_encode(m_uri.segments[i]);
        }
        return out;
    }

    [[nodiscard]] constexpr std::string repr() const {
        std::string out = "path(\"" + as_uri() + "\"";
        if (m_pin) out += ", engine=" + std::string(m_pin->label);
        return out + ")";
    }

    // ── composition (pathlib-style) ────────────────────────────────────────
    [[nodiscard]] constexpr basic_file joined(std::string_view other) const {
        basic_file out = *this;
        const uri rhs = Strategy::parse(other);
        Strategy::join_into(out.m_uri, rhs);
        return out;
    }
    [[nodiscard]] constexpr basic_file rjoined(std::string_view other) const {   // other / self
        basic_file out(other, m_pin);
        Strategy::join_into(out.m_uri, m_uri);
        return out;
    }

    // pathlib's expanduser(): a path with no drive and no root whose first part starts with "~"
    // has that part replaced by the home it names — "~" this user's, "~name" that user's — and
    // keeps every later part; any other path is itself. `home_of(user)` answers ("" for this user),
    // by default the system (platform/home.h). No home known, or one that starts with "~" itself,
    // is std::runtime_error, as pathlib's RuntimeError.
    //
    //     basic_file("~/a/b").expanduser(home)   -> "/home/someone/a/b"     ("~root/x" -> "/root/x")
    template <class HomeOf>
    [[nodiscard]] constexpr basic_file expanduser(HomeOf&& home_of) const {
        if (m_uri.absolute || anchor_count() != 0 || m_uri.segments.empty() || !m_uri.segments.front().starts_with('~')) return *this;
        const std::optional<std::string> home = home_of(std::string_view(m_uri.segments.front()).substr(1));
        if (!home || home->starts_with('~')) throw std::runtime_error("Could not determine home directory.");
        basic_file out(Strategy::parse(*home), m_pin);
        out.m_uri.segments.insert(out.m_uri.segments.end(), m_uri.segments.begin() + 1, m_uri.segments.end());
        return out;
    }
    [[nodiscard]] basic_file expanduser() const { return expanduser(platform::home_of); }

    [[nodiscard]] constexpr basic_file parent() const {
        basic_file out = *this;
        if (out.has_name()) out.m_uri.segments.pop_back();   // the anchor (or ".") is its own parent
        return out;
    }

    // Ancestors, closest first, up to and including the anchor: "a/b/c" ->
    // ["a/b", "a"], "/a/b" -> ["/a", "/"]. (The empty relative parent "." is
    // not listed.)
    [[nodiscard]] constexpr std::vector<basic_file> parents() const {
        std::vector<basic_file> out;
        basic_file cur = *this;
        while (cur.has_name()) {
            cur.m_uri.segments.pop_back();
            if (!cur.has_name() && !Strategy::is_anchored(cur.m_uri)) break;
            out.push_back(cur);
        }
        return out;
    }

    // pathlib's relative_to: this path with `base` taken off its front, or nullopt when this path is
    // neither `base` nor beneath it. Same anchor (root, drive, share) and every segment of `base` a
    // leading segment of this one, compared as written — the equality `==` uses, so unlike
    // PureWindowsPath it does not fold case. Lexical, as pathlib's is: ".." is a segment like any
    // other. "/a/b" relative to "/a/" -> "b"; "/ab/c" relative to "/a" -> nullopt; a path relative to
    // itself -> ".".
    [[nodiscard]] constexpr std::optional<basic_file> relative_to(const basic_file& base) const {
        const uri& a = m_uri;
        const uri& b = base.m_uri;
        if (a.scheme != b.scheme || a.has_authority != b.has_authority || a.authority != b.authority ||
            a.absolute != b.absolute || b.segments.size() > a.segments.size()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < b.segments.size(); ++i) {
            if (a.segments[i] != b.segments[i]) return std::nullopt;
        }
        uri rest;   // relative: no scheme, no authority, no root
        rest.segments.assign(a.segments.begin() + static_cast<std::ptrdiff_t>(b.segments.size()), a.segments.end());
        return basic_file(std::move(rest), m_pin);
    }
    [[nodiscard]] constexpr bool is_relative_to(const basic_file& base) const { return relative_to(base).has_value(); }

    // pathlib's rule: a name is any non-empty component without a separator
    // that is not "." (".." is allowed, as in pathlib).
    [[nodiscard]] constexpr basic_file with_name(std::string_view n) const {
        if (n.empty() || n == ".") throw std::invalid_argument("invalid name: '" + std::string(n) + "'");
        for (char c : n) {
            if (Strategy::is_sep(c)) throw std::invalid_argument("invalid name: '" + std::string(n) + "'");
        }
        if (!has_name()) throw std::invalid_argument("'" + fspath() + "' has an empty name");
        basic_file out = *this;
        out.m_uri.segments.back() = std::string(n);
        return out;
    }
    [[nodiscard]] constexpr basic_file with_suffix(std::string_view s) const {
        if (!s.empty() && (s.front() != '.' || s.size() == 1)) {
            throw std::invalid_argument("invalid suffix: '" + std::string(s) + "'");
        }
        return with_name(stem() + std::string(s));
    }
    [[nodiscard]] constexpr basic_file with_stem(std::string_view s) const { return with_name(std::string(s) + suffix()); }

    // ── filesystem (runtime; the OS boundary) ──────────────────────────────
    [[nodiscard]] fs::path to_fs() const { return detail::fs_from_text(fspath()); }

    [[nodiscard]] basic_file absolute() const { return from_fs(fs::absolute(to_fs())); }
    /// pathlib's resolve(): absolute first, then its links and `..` resolved — the parts that exist
    /// (weakly_canonical alone left a relative path relative when none of it existed, 2026-11-01),
    /// or, strict, all of them, a missing one a filesystem_error (FileNotFoundError in Python).
    [[nodiscard]] basic_file resolve(bool strict = false) const {
        const fs::path whole = fs::absolute(to_fs());
        return from_fs(strict ? fs::canonical(whole) : fs::weakly_canonical(whole));
    }

    [[nodiscard]] bool exists() const { return fs::exists(to_fs()); }
    [[nodiscard]] bool is_file() const { return fs::is_regular_file(to_fs()); }
    [[nodiscard]] bool is_dir() const { return fs::is_directory(to_fs()); }
    [[nodiscard]] bool is_symlink() const { return fs::is_symlink(to_fs()); }
    [[nodiscard]] std::uintmax_t size() const { return platform::file_size(to_fs()); }   // a folder is IsADirectoryError everywhere

    // Children of this directory, sorted for determinism.
    [[nodiscard]] std::vector<basic_file> iterdir() const {
        std::vector<basic_file> out;
        for (const auto& e : fs::directory_iterator(to_fs())) out.push_back(from_fs(e.path()));
        std::sort(out.begin(), out.end());
        return out;
    }

    // What pathlib's glob finds under this directory, sorted and deduplicated. The rules are
    // glob_walk.h's; this runs them on the calling thread (parallel_glob.h runs the same on a pool).
    [[nodiscard]] std::vector<basic_file> glob(std::string_view pattern) const { return glob(glob_pattern(pattern)); }
    /// The same with the pattern parsed already (a caller that checked it first parses it once).
    [[nodiscard]] std::vector<basic_file> glob(const glob_pattern& pattern) const { return found_by_glob(glob_walk_here(pattern, to_fs())); }

    // What a walk found, as files with this one's pin: sorted and deduplicated, as glob() returns them.
    [[nodiscard]] std::vector<basic_file> found_by_glob(const std::vector<fs::path>& found) const {
        std::vector<basic_file> out;
        out.reserve(found.size());
        for (const fs::path& p : found) out.push_back(from_fs(p));
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // glob("**/" + pattern): the pattern anywhere under this directory.
    [[nodiscard]] std::vector<basic_file> rglob(std::string_view pattern) const {
        return glob("**/" + std::string(pattern));
    }

    // The raw bytes of the file, undecoded (binary, no newline translation), read by the system's
    // calls into the room `grow` gives (platform::read_whole_into: grow(n) is a buffer of at least
    // n bytes that keeps what it held); how many. Fails as pathlib does: file_error with the errno
    // of the open or the read — EISDIR for a directory on POSIX, EACCES on Windows.
    template <class Grow>
    std::size_t read_bytes_into(Grow&& grow) const {
        const auto got = platform::read_whole_into(to_fs(), std::forward<Grow>(grow));
        if (!got) throw file_error(got.error(), fspath(), got.error() == EISDIR ? "is a directory" : "cannot read file");
        return *got;
    }

    // The same, as a string.
    [[nodiscard]] std::string read_bytes() const {
        std::string bytes;
        const std::size_t got = read_bytes_into([&bytes](std::size_t n) {
            bytes.resize_and_overwrite(n, [](char*, std::size_t m) noexcept { return m; });   // filled by the read, not zeroed
            return bytes.data();
        });
        bytes.resize(got);
        return bytes;
    }

    // Append `text` to the file as lines, the file made when it is not there: a "\n" first when
    // its last line has none (platform::append_lines). file_error with the errno of what failed.
    void append_lines(std::string_view text) const {
        if (const int err = platform::append_lines(to_fs(), text); err != 0) throw file_error(err, fspath(), "cannot append to file");
    }

    // Replace the file's contents with `data`, byte for byte: in place by default (pathlib parity),
    // or atomically (basic_atomic_write: a reader sees the old contents or the new, never part of
    // either). Closed before it returns, so a failure the close reports — a full disk — is raised.
    void write_bytes(std::string_view data, write_mode mode = write_mode::in_place) const {
        if (mode == write_mode::atomic) {
            basic_atomic_write<basic_file> replacement(*this);
            replacement.write(data);
            replacement.commit();
            return;
        }
        const fs::path p = to_fs();
        errno = 0;
        std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
        if (!ofs) throw file_error(detail::open_errno(p), fspath(), "cannot open file for writing");
        ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
        ofs.close();
        if (!ofs) throw file_error(errno != 0 ? errno : EIO, fspath(), "write failed");
    }

    // Create this directory (pathlib parity): `parents` creates missing
    // ancestors, `exist_ok` tolerates an existing directory. A non-directory
    // in the way always fails, as FileExistsError; a missing parent is ENOENT.
    void mkdir(bool parents = false, bool exist_ok = false) const {
        const fs::path p = to_fs();
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            if (exist_ok) return;
            throw file_error(EEXIST, fspath(), "directory exists");
        }
        if (fs::exists(p, ec)) {
            throw file_error(EEXIST, fspath(), "not a directory");
        }
        const bool made = parents ? fs::create_directories(p, ec) : fs::create_directory(p, ec);
        // The condition maps a platform's own code (a Win32 error) to the errno Python expects.
        if (ec) throw file_error(ec.default_error_condition().value(), fspath(), "cannot create directory");
        if (!made) throw file_error(EEXIST, fspath(), "cannot create directory");
    }

    // Remove this file or symbolic link (pathlib parity): a symlink goes, not what it points to;
    // nothing there is ENOENT unless `missing_ok`. A directory is refused with the operating
    // system's own errno, as os.unlink refuses it (EISDIR on Linux, EPERM on macOS, EACCES on Windows).
    void unlink(bool missing_ok = false) const {
        const fs::path p = to_fs();
        if (platform::unlink_file(p) == 0) return;
        const int err = errno;
        if (err == ENOENT && missing_ok) return;
        throw file_error(err, fspath(), "cannot remove file");
    }


private:
    [[nodiscard]] constexpr std::size_t anchor_count() const noexcept { return Strategy::anchor_segments(m_uri); }

    [[nodiscard]] basic_file from_fs(const fs::path& p) const {
        basic_file out;
        out.m_pin = m_pin;
        Strategy::parse_into(out.m_uri, detail::text_from_fs(p));
        return out;
    }

    uri m_uri;
    const engine_info* m_pin{nullptr};   // pinned at construction; nullptr = auto by extension
};

using file = basic_file<native_strategy>;

// The OS boundary for a caller that holds an fs::path (a walk's match): its text as pathlike holds
// it — native bytes on POSIX, UTF-8 on Windows — and back.
using detail::fs_from_text;
using detail::text_from_fs;

/// One atomic replacement of a file, owning its temporary from creation to rename or removal
/// (docs/design/pathlike_writes.md). Example:
///   atomic_write replacement(file("notes.json")); replacement.write("{}"); replacement.commit();
/// The temporary is a hidden sibling, `.notes.json.<16 hex digits>.tmp`, created exclusively, so two
/// writers never share one. commit() forces it to the disk, gives it the file's permission bits,
/// renames it over the file and forces the folder: a reader sees the old contents or the new, and
/// after a power cut one of them is there whole. Destroyed without commit(), it removes the
/// temporary. A symbolic link is written through: the file it names is replaced, the link stays. The
/// file is still a new one — a second hard link keeps the old contents, the owner is the writer's.
template <class File, class Sync>
class basic_atomic_write {
public:
    explicit basic_atomic_write(const File& target) : m_where(target.fspath()), m_destination(destination_of(target.to_fs())) {
        fs::path folder = m_destination.parent_path();
        for (int attempt = 0; attempt < 8; ++attempt) {   // two writers drawing one tag is 1 in 2^64: retried, not assumed away
            fs::path name(".");
            name += m_destination.filename();
            name += "." + detail::random_tag() + ".tmp";
            m_temporary = folder / name;
            m_fd = detail::open_exclusive(m_temporary);
            if (m_fd >= 0) return;
            if (errno != EEXIST) break;
        }
        throw file_error(errno, m_where, "cannot create a temporary file beside");
    }
    basic_atomic_write(const basic_atomic_write&) = delete;
    basic_atomic_write& operator=(const basic_atomic_write&) = delete;
    ~basic_atomic_write() { abandon(); }

    /// The temporary, for a writer that writes by path (an engine): what it holds at commit() is what
    /// replaces the file.
    [[nodiscard]] File temporary() const { return File(detail::text_from_fs(m_temporary)); }

    /// Add `part` to the temporary.
    void write(std::string_view part) {
        while (!part.empty()) {
            const long long written = detail::write_some(m_fd, part.data(), part.size());
            if (written < 0) {
                if (errno == EINTR) continue;
                throw file_error(errno, m_where, "write failed");
            }
            part.remove_prefix(static_cast<std::size_t>(written));
        }
    }

    /// Force the temporary to the disk, rename it over the file, force the folder.
    void commit() {
        Sync::file(m_fd, m_where);
        if (detail::close_fd(std::exchange(m_fd, -1)) != 0) throw file_error(errno, m_where, "write failed");
        std::error_code ec;
        if (const fs::file_status status = fs::status(m_destination, ec); !ec && fs::exists(status))
            fs::permissions(m_temporary, status.permissions(), ec);   // best effort: the contents are the point
        fs::rename(m_temporary, m_destination, ec);
        if (ec) throw file_error(ec.default_error_condition().value(), m_where, "cannot replace file");
        m_committed = true;
        const fs::path folder = m_destination.parent_path();
        Sync::folder(folder.empty() ? fs::path(".") : folder, m_where);
    }

private:
    // The file a write replaces: a symbolic link's target, so the link survives the rename.
    [[nodiscard]] static fs::path destination_of(const fs::path& p) {
        std::error_code ec;
        return fs::is_symlink(p, ec) ? fs::weakly_canonical(p) : p;
    }

    void abandon() noexcept {
        if (m_fd >= 0) detail::close_fd(std::exchange(m_fd, -1));
        if (!m_committed) {
            std::error_code ignored;
            fs::remove(m_temporary, ignored);
        }
    }

    std::string m_where;        // the path as the caller spelt it, for errors
    fs::path m_destination;
    fs::path m_temporary;
    int m_fd = -1;
    bool m_committed = false;
};

using atomic_write = basic_atomic_write<file>;

}  // namespace pygim::pathlike
