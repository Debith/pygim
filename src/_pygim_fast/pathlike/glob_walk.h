#pragma once
// pathlike/glob_walk.h — glob's rules, once: one directory matched against one segment of a
// pattern, giving the paths it matched and the directories to go on into.
//
//     glob_pattern pattern("**/*.py");
//     glob_step(pattern, glob_task{root, 0}, emit, go_on);   // emit(path) per match, go_on(task) per directory
//
// It is pathlib's glob (Python 3.14, recurse_symlinks=False; D49, 2026-10-30), held to it by
// tests/unittests/test_glob_walk.py: segments split on '/' (and a backslash on Windows); `*`, `?`
// and `[seq]` within a name, dot names too, a character a UTF-8 code point; `**` any number of
// directories, yielding the one it starts in, listing a link to a directory but never going through
// it — any other segment follows such a link; a trailing separator keeps directories only, and a
// final `**/` real directories; a `.` segment is dropped and `..` is a step up; case-sensitive but
// on Windows, or as case_sensitive says. Unreadable directories are passed over. Not pathlib's:
// recurse_symlinks=True (pathlib follows a link loop until the OS refuses, at 40 links); matching
// any case folds ASCII letters only (pathlib's os.path.normcase folds all of Unicode on Windows).
// Beyond pathlib, for path.pathset(): a walk may keep only what is_file() — or is_dir() — says of
// its matches (glob_kind; the listing's kind, a stat for a link only), and may skip folders by name
// (glob_pattern's skip: never entered, never matched, nor anything beneath them).
//
// The walk is a set of glob_tasks, each a directory and the segment its entries are matched
// against; how they run is the caller's: file::glob (core.h) runs them one after another,
// parallel_glob.h on a task_pool. Both are glob_step, so the two cannot drift. A directory is
// listed with readdir, taking each entry's kind from the listing (d_type), with no stat but for a
// link and an unknown kind: 2.5 ms for CPython's Lib against 4.3 ms through std::filesystem (#274).
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../platform/listing.h"
#include "path_rules.h"

namespace pygim::pathlike {

namespace fs = std::filesystem;

namespace detail {

// How many bytes the UTF-8 character starting with `lead` takes; 1 for a byte that starts none, so
// a name that is not UTF-8 still moves on. `?` and a class match one character, as pathlib's do.
[[nodiscard]] constexpr std::size_t utf8_length(char lead) noexcept {
    const auto b = static_cast<unsigned char>(lead);
    return b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF8 ? 4 : 1;
}

// The character at text[at] as its code point, and how many bytes it took.
[[nodiscard]] constexpr std::pair<char32_t, std::size_t> utf8_at(std::string_view text, std::size_t at) noexcept {
    const std::size_t n = std::min(utf8_length(text[at]), text.size() - at);
    if (n == 1) return {static_cast<unsigned char>(text[at]), 1};
    char32_t c = static_cast<unsigned char>(text[at]) & (0x7F >> n);
    for (std::size_t i = 1; i < n; ++i) c = (c << 6) | (static_cast<unsigned char>(text[at + i]) & 0x3F);
    return {c, n};
}
static_assert(utf8_at("é", 0) == std::pair<char32_t, std::size_t>(0xE9, 2) && utf8_at("a", 0).first == U'a');

// A code point as matched: an ASCII letter in lower case when any case matches. pathlib folds with
// os.path.normcase, all of Unicode on Windows; here a letter past ASCII matches its own case only.
[[nodiscard]] constexpr char32_t folded(char32_t c, bool any_case) noexcept { return any_case && c >= U'A' && c <= U'Z' ? c - U'A' + U'a' : c; }

// Where the class opened at pattern[open] ('[') ends: just past its ']', or npos when it never closes,
// and then the '[' is a plain character. A ']' first in it (after '!', if any) is a member: fnmatch's reading.
[[nodiscard]] constexpr std::size_t class_end(std::string_view pattern, std::size_t open) noexcept {
    std::size_t i = open + 1;
    if (i < pattern.size() && pattern[i] == '!') ++i;
    if (i < pattern.size() && pattern[i] == ']') ++i;
    while (i < pattern.size() && pattern[i] != ']') ++i;
    return i < pattern.size() ? i + 1 : std::string_view::npos;
}

// Whether `c` is in the class pattern[open, end): its members and its a-z ranges, all of it negated
// by a '!' first. A '-' first or last is a member; a range whose ends are reversed holds nothing.
[[nodiscard]] constexpr bool in_class(std::string_view pattern, std::size_t open, std::size_t end, char32_t c, bool any_case) noexcept {
    std::size_t i = open + 1;
    const bool negated = pattern[i] == '!';
    if (negated) ++i;
    const std::size_t close = end - 1;
    const char32_t x = folded(c, any_case);
    bool member = false;
    while (i < close) {
        const auto [lo, lo_len] = utf8_at(pattern, i);
        if (i + lo_len + 1 < close && pattern[i + lo_len] == '-') {
            const auto [hi, hi_len] = utf8_at(pattern, i + lo_len + 1);
            member = member || (folded(lo, any_case) <= x && x <= folded(hi, any_case));
            i += lo_len + 1 + hi_len;
        } else {
            member = member || folded(lo, any_case) == x;
            i += lo_len;
        }
    }
    return member != negated;
}

}  // namespace detail

/// One glob *segment* against one path component, as pathlib's glob reads one: `*` any run, `?`
/// one character, `[seq]` one character of a class (`[!seq]` one not in it, `[a-c]` a range,
/// `[[]` a '['), never crossing a directory separator (the walk in glob_step handles `/` and
/// `**`). `case_sensitive` false: ASCII letters match in either case. pygim.pathlike.match_pattern.
[[nodiscard]] constexpr bool glob_match(std::string_view pattern, std::string_view name, bool case_sensitive = true) noexcept {
    const bool any_case = !case_sensitive;
    std::size_t p = 0, n = 0;
    std::size_t star_p = std::string_view::npos, star_n = 0;
    while (n < name.size()) {
        if (p < pattern.size()) {
            const char c = pattern[p];
            if (c == '*') {
                star_p = p++;
                star_n = n;
                continue;
            }
            const auto [here, here_len] = detail::utf8_at(name, n);
            const std::size_t end = c == '[' ? detail::class_end(pattern, p) : std::string_view::npos;
            if (end != std::string_view::npos) {
                if (detail::in_class(pattern, p, end, here, any_case)) {
                    p = end;
                    n += here_len;
                    continue;
                }
            } else if (c == '?') {
                ++p;
                n += here_len;
                continue;
            } else {
                const auto [want, want_len] = detail::utf8_at(pattern, p);
                if (detail::folded(want, any_case) == detail::folded(here, any_case)) {
                    p += want_len;
                    n += here_len;
                    continue;
                }
            }
        }
        if (star_p == std::string_view::npos) return false;
        p = star_p + 1;   // the last `*` takes one more character
        star_n += detail::utf8_length(name[star_n]);
        n = star_n;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}
static_assert(glob_match("*.py", "a.py") && glob_match("*.py", ".hidden.py") && !glob_match("*.py", "a.pyc") && glob_match("f?.txt", "f1.txt"));
static_assert(glob_match("[ab]", "a") && !glob_match("[ab]", "[ab]") && glob_match("[[]ab]", "[ab]") && glob_match("[!a]*", "b.py") &&
              glob_match("[a-c].txt", "b.txt") && glob_match("?.py", "é.py") && glob_match("*.PY", "a.py", false));

/// A pattern pathlib refuses as unsupported, NotImplementedError in Python: one that is not relative.
class glob_unsupported : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/// Which of its matches a walk keeps: every one, or those pathlib's is_file() — or is_dir() — says
/// true of, a link followed (a broken link, a FIFO or a socket is neither).
enum class glob_kind { any, file, directory };

/// A glob pattern as its segments, read as pathlib reads one under Rules (path_rules.h): split on
/// Rules' separators ('/', and a backslash under Windows'), a `.` segment dropped, a run of `**` one
/// segment, a trailing separator keeping directories only. Matched case-sensitively unless told
/// otherwise — or, untold, as Rules say (Windows' names do not differ by case). An empty pattern, or
/// one of `.` alone, is std::invalid_argument; one that is not relative, glob_unsupported. Beyond
/// pathlib: `kind`, the matches kept; `skip`, globs naming folders the walk passes over — one so
/// named, or a link to one, is never entered or matched, nor is anything beneath it.
///
///     basic_glob_pattern<posix_strategy>("**/*.py", std::nullopt, glob_kind::file, {"build", ".*"})
template <class Rules>
class basic_glob_pattern {
public:
    constexpr explicit basic_glob_pattern(std::string_view pattern, std::optional<bool> case_sensitive = std::nullopt,
                                          glob_kind kind = glob_kind::any, std::vector<std::string> skip = {})
        : m_skip(std::move(skip)), m_kind(kind), m_case_sensitive(case_sensitive.value_or(Rules::case_sensitive)) {
        if (pattern.empty()) throw std::invalid_argument("Unacceptable pattern: ''");
        // A leading separator is refused explicitly: under Windows' rules "/x" has no drive, so it
        // is not absolute, yet the pattern is clearly not relative in the caller's intent.
        if (Rules::is_sep(pattern.front()) || Rules::is_absolute(Rules::parse(pattern))) throw glob_unsupported("Non-relative patterns are unsupported");
        for (std::size_t start = 0; start <= pattern.size();) {
            std::size_t end = start;
            while (end < pattern.size() && !Rules::is_sep(pattern[end])) ++end;
            const std::string_view segment = pattern.substr(start, end - start);
            if (!segment.empty() && segment != "." && !(segment == "**" && !m_segments.empty() && m_segments.back() == "**")) {
                m_segments.emplace_back(segment);
            }
            start = end + 1;
        }
        m_dirs_only = Rules::is_sep(pattern.back());
        if (m_segments.empty()) throw std::invalid_argument("Unacceptable pattern: '" + std::string(pattern) + "'");
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_segments.size(); }
    [[nodiscard]] constexpr bool is_last(std::size_t k) const noexcept { return k + 1 == m_segments.size(); }
    /// Whether segment k is `**`, any number of directories.
    [[nodiscard]] constexpr bool spans_directories(std::size_t k) const noexcept { return m_segments[k] == "**"; }
    /// Whether segment k is `..`: a step to the directory above, never a name in a listing.
    [[nodiscard]] constexpr bool steps_up(std::size_t k) const noexcept { return m_segments[k] == ".."; }
    /// Whether segment k names one entry (no `*`, `?` or `[`): found by one stat, not a listing.
    [[nodiscard]] constexpr bool is_literal(std::size_t k) const noexcept {
        return m_segments[k].find_first_of("*?[") == std::string::npos && !steps_up(k);
    }
    /// Whether the pattern ended in a separator: only directories match its last segment.
    [[nodiscard]] constexpr bool dirs_only() const noexcept { return m_dirs_only; }
    [[nodiscard]] constexpr bool case_sensitive() const noexcept { return m_case_sensitive; }
    /// Which matches the walk keeps.
    [[nodiscard]] constexpr glob_kind kind() const noexcept { return m_kind; }
    /// Whether a folder of this name is passed over: one of the skip globs matches it.
    [[nodiscard]] constexpr bool skips(std::string_view name) const noexcept {
        return std::ranges::any_of(m_skip, [&](const std::string& g) { return glob_match(g, name, m_case_sensitive); });
    }
    [[nodiscard]] constexpr const std::string& segment(std::size_t k) const noexcept { return m_segments[k]; }
    /// Whether a name matches segment k.
    [[nodiscard]] constexpr bool matches(std::size_t k, std::string_view name) const noexcept { return glob_match(m_segments[k], name, m_case_sensitive); }
    /// Whether the walk may list more than one directory, and so run long: a `**` anywhere, or a
    /// wildcard before the last segment (`*/*/*.py` lists every directory of each level). Literal
    /// directories and then any last segment list one directory at most.
    [[nodiscard]] constexpr bool lists_many_directories() const noexcept {
        for (std::size_t k = 0; k < m_segments.size(); ++k) {
            if (spans_directories(k) || (!is_last(k) && !is_literal(k) && !steps_up(k))) return true;
        }
        return false;
    }

private:
    std::vector<std::string> m_segments;
    std::vector<std::string> m_skip;
    glob_kind m_kind;
    bool m_dirs_only = false;
    bool m_case_sensitive;
};
static_assert(basic_glob_pattern<posix_strategy>("**/*.py", std::nullopt, glob_kind::file, {"build", ".*"}).skips(".git") &&
              !basic_glob_pattern<posix_strategy>("**/*.py", std::nullopt, glob_kind::file, {"build", ".*"}).skips("src") &&
              basic_glob_pattern<posix_strategy>("*", false, glob_kind::any, {"build"}).skips("Build") &&
              !basic_glob_pattern<posix_strategy>("*").skips("build"));

/// A glob pattern under the running system's rules.
using glob_pattern = basic_glob_pattern<native_strategy>;

using platform::dir_entry;
using platform::entry_kind;
using platform::list_directory;

/// Whether a link at `path` leads to a directory (it is followed, as stat follows it).
[[nodiscard]] inline bool leads_to_directory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

/// Whether a link at `path` leads to a regular file.
[[nodiscard]] inline bool leads_to_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

/// One unit of a walk: a directory and the segment of the pattern its entries are matched against;
/// for a `**`, whether the directory is where that `**` starts (it yields the directory itself there).
class glob_task {
public:
    glob_task(fs::path dir, std::size_t segment, bool starts_run = false) noexcept
        : m_dir(std::move(dir)), m_segment(segment), m_starts_run(starts_run) {}
    /// The walk's first task: `root` against the first segment.
    [[nodiscard]] static glob_task first(const glob_pattern& pattern, fs::path root) { return entering(pattern, std::move(root), 0); }
    /// `dir` against segment k, entered from the segment before: where a `**` starts, if k is one.
    [[nodiscard]] static glob_task entering(const glob_pattern& pattern, fs::path dir, std::size_t k) {
        return glob_task(std::move(dir), k, k < pattern.size() && pattern.spans_directories(k));
    }
    [[nodiscard]] const fs::path& dir() const noexcept { return m_dir; }
    [[nodiscard]] std::size_t segment() const noexcept { return m_segment; }
    [[nodiscard]] bool starts_run() const noexcept { return m_starts_run; }

private:
    fs::path m_dir;
    std::size_t m_segment;
    bool m_starts_run;
};

namespace detail {

// Whether a matched entry is gone on with: a directory, or a link to one (followed, as pathlib's
// segments other than `**` follow it).
[[nodiscard]] inline bool is_directory_followed(entry_kind kind, const fs::path& path) {
    return kind == entry_kind::directory || (kind == entry_kind::link && leads_to_directory(path));
}
[[nodiscard]] inline bool is_directory_followed(const dir_entry& e, const fs::path& path) { return is_directory_followed(e.kind(), path); }

// Whether a match of `kind` at `path` is one the pattern keeps: what is_file() or is_dir() says of
// it, a link followed — a stat for a link only, the listing tells the rest.
[[nodiscard]] inline bool kept(const glob_pattern& pattern, entry_kind kind, const fs::path& path) {
    switch (pattern.kind()) {
        case glob_kind::file: return kind == entry_kind::file || (kind == entry_kind::link && leads_to_file(path));
        case glob_kind::directory: return is_directory_followed(kind, path);
        case glob_kind::any: break;
    }
    return true;
}

// Whether a listed entry is a folder the pattern skips: its name is one of skip's, and it is a
// directory or a link to one (the name asked first: a stat for a link so named only).
[[nodiscard]] inline bool skipped(const glob_pattern& pattern, const dir_entry& e, const fs::path& path) {
    return pattern.skips(e.name()) && is_directory_followed(e, path);
}

// The entries of `dir` against segment k (not `**`): a match on the last segment is emitted (a
// directory only, when the pattern ends in a separator), a match that is a directory — or a link
// to one — is gone into for the next.
template <class Emit, class GoOn>
void match_segment(const glob_pattern& pattern, const fs::path& dir, const std::vector<dir_entry>& entries, std::size_t k,
                   Emit& emit, GoOn& go_on) {
    for (const dir_entry& e : entries) {
        if (!pattern.matches(k, e.name())) continue;
        fs::path path = e.under(dir);
        if (skipped(pattern, e, path)) continue;
        if (pattern.is_last(k)) {
            if ((!pattern.dirs_only() || is_directory_followed(e, path)) && kept(pattern, e.kind(), path)) emit(std::move(path));
        } else if (is_directory_followed(e, path)) {
            go_on(glob_task::entering(pattern, std::move(path), k + 1));
        }
    }
}

// Segment k is `..`: `dir/..`, a step up as pathlib takes it (the path keeps its `..`).
template <class Emit, class GoOn>
void step_up(const glob_pattern& pattern, const fs::path& dir, std::size_t k, Emit& emit, GoOn& go_on) {
    fs::path up = dir / "..";
    if (!pattern.is_last(k)) go_on(glob_task::entering(pattern, std::move(up), k + 1));
    else if (pattern.kind() != glob_kind::file) emit(std::move(up));   // the folder above: a directory
}

// Segment k names one entry: one stat of `dir/name`, where a listing read the whole directory to
// find it. Only where the system's file systems tell names apart by case (platform::
// finds_a_name_by_stat), and matched case-sensitively: there the stat finds exactly what the
// listing's match would (macOS's and Windows' default ones would find `Src` for `src`).
template <class Emit, class GoOn>
void find_literal(const glob_pattern& pattern, const fs::path& dir, std::size_t k, Emit& emit, GoOn& go_on) {
    fs::path path = dir / pattern.segment(k);
    const std::optional<entry_kind> kind = platform::entry_at(path);
    if (!kind) return;
    const bool directory = is_directory_followed(*kind, path);
    if (directory && pattern.skips(pattern.segment(k))) return;
    if (pattern.is_last(k)) {
        if ((!pattern.dirs_only() || directory) && kept(pattern, *kind, path)) emit(std::move(path));
    } else if (directory) {
        go_on(glob_task::entering(pattern, std::move(path), k + 1));
    }
}

}  // namespace detail

/// One task of a walk: `task`'s directory listed and matched against its segment. Each match goes
/// to emit(fs::path), each directory to go into to go_on(glob_task). As pathlib's glob with
/// recurse_symlinks=False: `**` lists a link but never goes through it.
template <class Emit, class GoOn>
void glob_step(const glob_pattern& pattern, const glob_task& task, Emit&& emit, GoOn&& go_on) {
    const std::size_t k = task.segment();
    if (pattern.steps_up(k)) return detail::step_up(pattern, task.dir(), k, emit, go_on);
    if constexpr (platform::finds_a_name_by_stat) {
        if (!pattern.spans_directories(k) && pattern.is_literal(k) && pattern.case_sensitive()) {
            return detail::find_literal(pattern, task.dir(), k, emit, go_on);
        }
    }
    const std::vector<dir_entry> entries = list_directory(task.dir());
    if (!pattern.spans_directories(k)) {
        detail::match_segment(pattern, task.dir(), entries, k, emit, go_on);
        return;
    }
    if (pattern.is_last(k)) {   // a final `**`: where it starts, then every entry below (a final `**/`: every real directory)
        if (task.starts_run() && pattern.kind() != glob_kind::file) emit(task.dir());
        for (const dir_entry& e : entries) {
            fs::path path = e.under(task.dir());
            if (detail::skipped(pattern, e, path)) continue;
            if (e.kind() == entry_kind::directory) {
                go_on(glob_task(path, k));
                if (pattern.kind() != glob_kind::file) emit(std::move(path));
            } else if (!pattern.dirs_only() && detail::kept(pattern, e.kind(), path)) {
                emit(std::move(path));
            }
        }
        return;
    }
    // `**` then more: the rest matched here (no directory between) — a `..` next is a step from
    // here — and `**` carried into each directory, not through a link.
    if (pattern.steps_up(k + 1)) go_on(glob_task::entering(pattern, task.dir(), k + 1));
    else detail::match_segment(pattern, task.dir(), entries, k + 1, emit, go_on);
    for (const dir_entry& e : entries) {
        if (e.kind() == entry_kind::directory && !pattern.skips(e.name())) go_on(glob_task(e.under(task.dir()), k));
    }
}

/// A walk on the calling thread, a directory at a time: what file::glob runs to its end, and how a
/// glob that may run long starts — one still going after a few directories goes on on a pool
/// (parallel_glob.h), with what it found and the directories left.
class glob_walk {
public:
    glob_walk(const glob_pattern& pattern, fs::path root) : m_left{glob_task::first(pattern, std::move(root))} {}

    /// Up to `directories` more listed, by `pattern` — the one it began with — while no more than
    /// that many wait to be: a walk that fans out stops at once. Whether it is done.
    bool walk(const glob_pattern& pattern, std::size_t directories) {
        for (; directories > 0 && !m_left.empty() && m_left.size() <= directories; --directories) {
            const glob_task task = std::move(m_left.back());
            m_left.pop_back();
            glob_step(pattern, task, [&](fs::path p) { m_found.push_back(std::move(p)); }, [&](glob_task t) { m_left.push_back(std::move(t)); });
        }
        return m_left.empty();
    }
    /// What it matched so far, taken.
    [[nodiscard]] std::vector<fs::path> found() && noexcept { return std::move(m_found); }
    /// The directories it has yet to list, taken.
    [[nodiscard]] std::vector<glob_task> left() && noexcept { return std::move(m_left); }

private:
    std::vector<fs::path> m_found;
    std::vector<glob_task> m_left;   // a stack: the walk goes deep first
};

/// A walk on the calling thread to its end. What file::glob runs.
[[nodiscard]] inline std::vector<fs::path> glob_walk_here(const glob_pattern& pattern, const fs::path& root) {
    glob_walk walk(pattern, root);
    walk.walk(pattern, static_cast<std::size_t>(-1));
    return std::move(walk).found();
}

}  // namespace pygim::pathlike
