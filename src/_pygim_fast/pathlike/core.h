#pragma once
// pathlike/core.h — the pybind-free heart of `pygim.path`.
//
// A `file` is a path that knows how to read and decode itself. Its VALUE is a
// `uri` (uri.h): a list of segments plus authority and root flag. A FLAVOUR
// (posix_flavour / windows_flavour) says how native path text maps onto that
// value and back — separators, drives, UNC hosts, roots — the way pathlib's
// PurePosixPath and PureWindowsPath do. Both flavours are pure policy types, so
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "uri.h"

namespace pygim::pathlike {

namespace fs = std::filesystem;

// A borrowed view over an engine's constexpr string list — a deliberately
// minimal stand-in for std::span that is usable in constant expressions on
// every compiler in the build matrix.
struct sv_list {
    const std::string_view* first = nullptr;
    std::size_t count = 0;

    constexpr sv_list() = default;
    template <std::size_t N>
    constexpr sv_list(const std::array<std::string_view, N>& items) noexcept  // implicit by design
        : first(items.data()), count(N) {}

    [[nodiscard]] constexpr const std::string_view* begin() const noexcept { return first; }
    [[nodiscard]] constexpr const std::string_view* end() const noexcept { return first + count; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
    [[nodiscard]] constexpr bool contains(std::string_view s) const noexcept {
        for (std::string_view x : *this) {
            if (x == s) return true;
        }
        return false;
    }
};

// The pybind-free face of one engine: everything the registry, the proofs and
// `file` need to know. Every engine header declares one as `static constexpr
// engine_info info`; its address is the engine's identity at run time (static
// constexpr members are inline variables, so there is exactly one per program).
struct engine_info {
    std::string_view name;     // format name: engine="json"; the Python class is "<name>file"
    std::string_view label;    // LIBRARY label reported by .engine: "simdjson"
    std::string_view doc;      // one sentence, used by every derived docstring
    sv_list exts;              // auto-dispatch extensions: lower-case, leading dot
    sv_list aliases;           // extra engine= spellings: "yml", "ndjson", "tomlplusplus"

    // engine= selectors: the format name, the library label and the aliases.
    [[nodiscard]] constexpr bool selects(std::string_view n) const noexcept {
        return n == name || n == label || aliases.contains(n);
    }
    [[nodiscard]] constexpr bool owns(std::string_view ext) const noexcept { return exts.contains(ext); }
};

namespace detail {
// ASCII case folding of a short extension, so ".YAML" resolves like ".yaml".
// constexpr so the fold-then-lookup chain is provable at compile time.
[[nodiscard]] constexpr std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

[[nodiscard]] constexpr std::string ascii_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// One glob *segment* against one path component: `*` and `?`, never crossing
// a directory separator (the walk in file::glob() handles `/` and `**`).
[[nodiscard]] constexpr bool glob_match(std::string_view pattern, std::string_view name) noexcept {
    std::size_t p = 0, n = 0;
    std::size_t star_p = std::string_view::npos, star_n = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p; ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_n = n;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            n = ++star_n;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// The OS boundary: the internal text form is the NATIVE byte string on POSIX
// (so non-UTF-8 file names round-trip) and UTF-8 on Windows (lossless from the
// wide native form, and what Python's str carries). fs::path::string() would
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

// Split native path text on separators, dropping "" and "." components and
// keeping ".." — pathlib's rule for the non-anchor part of a path.
template <class IsSep>
constexpr void append_components(uri& u, std::string_view s, IsSep is_sep) {
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = start;
        while (end < s.size() && !is_sep(s[end])) ++end;
        const std::string_view seg = s.substr(start, end - start);
        if (!seg.empty() && seg != ".") u.segments.emplace_back(seg);
        if (end == s.size()) break;
        start = end + 1;
    }
}
}  // namespace detail

// ── Flavours: native text <-> uri value, pathlib's rules ───────────────────
// A flavour is a stateless policy: parse() maps a native path string onto the
// uri model, render() maps it back (pathlib's str()), and the anchor helpers
// say which leading segments are the drive/share that name() and parent()
// must never consume. from_uri_text() applies pathlib.Path.from_uri's rules
// to a decoded RFC 8089 file URI.

// POSIX: "/" separates; the root is "/" — or "//", which POSIX reserves for an
// implementation-defined meaning and pathlib therefore preserves (exactly two
// leading slashes; three or more collapse to "/"). Represented as the
// absolute flag plus a leading EMPTY segment, which is also what RFC 3986
// says the path "//x" is.
struct posix_flavour {
    static constexpr std::string_view name = "posix";
    static constexpr char sep = '/';
    [[nodiscard]] static constexpr bool is_sep(char c) noexcept { return c == '/'; }

    // Fills `u` in place. (Constant evaluation on GCC 13/14 mishandles a
    // string-holding struct returned by value straight into a member, so every
    // flavour operation mutates in place; parse() is the by-value convenience.)
    static constexpr void parse_into(uri& u, std::string_view s) {
        u = uri{};
        u.scheme = "file";
        std::size_t slashes = 0;
        while (slashes < s.size() && s[slashes] == '/') ++slashes;
        if (slashes) {
            u.absolute = true;
            if (slashes == 2) u.segments.emplace_back();   // the "//" root
            s.remove_prefix(slashes);
        }
        detail::append_components(u, s, is_sep);
    }
    [[nodiscard]] static constexpr uri parse(std::string_view s) {
        uri u;
        parse_into(u, s);
        return u;
    }

    // Leading segments that belong to the anchor (the "//" root's empty segment).
    [[nodiscard]] static constexpr std::size_t anchor_segments(const uri& u) noexcept {
        return (u.absolute && !u.segments.empty() && u.segments.front().empty()) ? 1 : 0;
    }
    [[nodiscard]] static constexpr std::string anchor(const uri& u) {
        if (!u.absolute) return "";
        return anchor_segments(u) ? "//" : "/";
    }
    [[nodiscard]] static constexpr bool is_absolute(const uri& u) noexcept { return u.absolute; }
    [[nodiscard]] static constexpr bool is_anchored(const uri& u) noexcept { return u.absolute; }

    // pathlib's str(): the anchor, then the components joined; "." when empty.
    [[nodiscard]] static constexpr std::string render(const uri& u) {
        std::string out = anchor(u);
        bool first = true;
        for (std::size_t i = anchor_segments(u); i < u.segments.size(); ++i) {
            if (!first) out += '/';
            first = false;
            out += u.segments[i];
        }
        return out.empty() ? std::string(".") : out;
    }

    // pathlib's join, in place: an absolute `other` replaces the whole path.
    static constexpr void join_into(uri& base, const uri& other) {
        if (other.absolute) {
            base = other;
            return;
        }
        base.segments.insert(base.segments.end(), other.segments.begin(), other.segments.end());
    }

    // RFC 8089 -> native text (pathlib.Path.from_uri): an empty or "localhost"
    // authority is dropped; any other host becomes the "//host" root form.
    [[nodiscard]] static constexpr std::string from_uri_text(const uri& u) {
        std::string text;
        if (u.has_authority && !u.authority.empty() && u.authority != "localhost") text = "//" + u.authority;
        return text + u.path();
    }

    // The RFC form of an absolute path, in place: file:///a/b — authority present, empty.
    static constexpr void make_file_uri(uri& u) {
        u.scheme = "file";
        u.has_authority = true;
        u.authority.clear();
    }
};

// Windows: "\" and "/" both separate. The anchor is a drive ("C:") or a UNC
// share ("\\server\share") plus the root "\". A drive is stored as the first
// segment ("C:", as RFC 8089 writes file:///C:/x) and a UNC host as the
// authority (file://server/share/x); the share is the first segment.
struct windows_flavour {
    static constexpr std::string_view name = "windows";
    static constexpr char sep = '\\';
    [[nodiscard]] static constexpr bool is_sep(char c) noexcept { return c == '\\' || c == '/'; }
    [[nodiscard]] static constexpr bool is_drive(std::string_view seg) noexcept {
        return seg.size() == 2 && uri::is_alpha(seg[0]) && seg[1] == ':';
    }

    static constexpr void parse_into(uri& u, std::string_view s) {
        u = uri{};
        u.scheme = "file";
        if (s.size() >= 2 && is_sep(s[0]) && is_sep(s[1])) {   // UNC, exactly as ntpath.splitroot
            s.remove_prefix(2);
            u.has_authority = true;
            std::size_t end = 0;
            while (end < s.size() && !is_sep(s[end])) ++end;
            u.authority = std::string(s.substr(0, end));       // may be empty ("///x" is host "", share "x")
            if (end == s.size()) return;                         // "\\host": the drive alone, no root
            s.remove_prefix(end + 1);
            end = 0;
            while (end < s.size() && !is_sep(s[end])) ++end;
            u.segments.emplace_back(s.substr(0, end));          // the share (part of the drive)
            if (end == s.size()) {                               // "\\host\share" with nothing after:
                // pathlib roots a UNC drive implicitly, unless the host is empty
                // or a device marker ("\\?\", "\\.\") — PureWindowsPath._parse_path.
                u.absolute = !u.authority.empty() && u.authority != "?" && u.authority != ".";
                return;
            }
            u.absolute = true;                                   // the separator after the share is the root
            s.remove_prefix(end + 1);
            detail::append_components(u, s, is_sep);
            return;
        }
        if (s.size() >= 2 && uri::is_alpha(s[0]) && s[1] == ':') {   // drive
            u.segments.emplace_back(s.substr(0, 2));
            s.remove_prefix(2);
        }
        if (!s.empty() && is_sep(s.front())) u.absolute = true;
        while (!s.empty() && is_sep(s.front())) s.remove_prefix(1);
        detail::append_components(u, s, is_sep);
    }
    [[nodiscard]] static constexpr uri parse(std::string_view s) {
        uri u;
        parse_into(u, s);
        return u;
    }

    [[nodiscard]] static constexpr bool has_drive(const uri& u) noexcept {
        return !u.has_authority && !u.segments.empty() && is_drive(u.segments.front());
    }
    [[nodiscard]] static constexpr std::size_t anchor_segments(const uri& u) noexcept {
        if (u.has_authority) return u.segments.empty() ? 0 : 1;   // the share
        return has_drive(u) ? 1 : 0;
    }
    // pathlib's drive: "C:" or "\\server\share".
    [[nodiscard]] static constexpr std::string drive(const uri& u) {
        if (u.has_authority) {
            std::string d = "\\\\" + u.authority;
            if (!u.segments.empty()) d += "\\" + u.segments.front();
            return d;
        }
        return has_drive(u) ? u.segments.front() : std::string();
    }
    [[nodiscard]] static constexpr std::string anchor(const uri& u) { return drive(u) + (u.absolute ? "\\" : ""); }
    [[nodiscard]] static constexpr bool is_absolute(const uri& u) noexcept {
        return u.absolute && (u.has_authority || has_drive(u));
    }
    [[nodiscard]] static constexpr bool is_anchored(const uri& u) noexcept {
        return u.absolute || u.has_authority || has_drive(u);
    }

    [[nodiscard]] static constexpr std::string render(const uri& u) {
        std::string out = anchor(u);
        bool first = true;
        for (std::size_t i = anchor_segments(u); i < u.segments.size(); ++i) {
            if (!first) out += '\\';
            first = false;
            out += u.segments[i];
        }
        return out.empty() ? std::string(".") : out;
    }

    [[nodiscard]] static constexpr bool same_drive(const uri& a, const uri& b) noexcept {
        if (!has_drive(a) || !has_drive(b)) return false;
        const char x = a.segments.front()[0], y = b.segments.front()[0];
        const char lx = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
        const char ly = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
        return lx == ly;
    }

    // pathlib's join, in place: a UNC or foreign-drive `other` replaces; a
    // rooted `other` keeps our drive; a same-drive drive-relative `other`
    // appends; anything else appends.
    static constexpr void join_into(uri& base, const uri& other) {
        if (other.has_authority) {
            base = other;
            return;
        }
        if (has_drive(other)) {
            if (other.absolute || !same_drive(base, other)) {
                base = other;
                return;
            }
            base.segments.insert(base.segments.end(), other.segments.begin() + 1, other.segments.end());
            return;
        }
        if (other.absolute) {
            base.segments.resize(anchor_segments(base));
            base.absolute = true;
        }
        base.segments.insert(base.segments.end(), other.segments.begin(), other.segments.end());
    }

    // RFC 8089 -> native text (pathlib.Path.from_uri): a host becomes a UNC
    // prefix, the slash before a drive is dropped ("/C:/x" -> "C:/x"), and the
    // legacy "C|" drive spelling becomes "C:".
    [[nodiscard]] static constexpr std::string from_uri_text(const uri& u) {
        std::string text;
        if (u.has_authority && !u.authority.empty() && u.authority != "localhost") text = "\\\\" + u.authority;
        std::string p = u.path();
        if (text.empty() && p.size() >= 3 && p[0] == '/' && uri::is_alpha(p[1]) && (p[2] == ':' || p[2] == '|')) {
            p = p.substr(1);
            p[1] = ':';
        }
        return text + p;
    }

    static constexpr void make_file_uri(uri& u) {
        u.scheme = "file";
        if (!u.has_authority) {
            u.has_authority = true;
            u.authority.clear();
        }
    }
};

#ifdef _WIN32
using native_flavour = windows_flavour;
#else
using native_flavour = posix_flavour;
#endif

// A path that reads and decodes itself. Models os.PathLike (it exposes
// __fspath__ through the binding), so it drops straight into open(), Path(),
// etc. An engine may be pinned at construction (nullptr = auto by extension);
// derived paths (parent(), with_suffix(), ...) inherit the pin. Resolution
// itself lives in the registry (registry.h: engine_list::resolve).
template <class Flavour>
class basic_file {
public:
    using flavour = Flavour;

    constexpr basic_file() = default;   // "."

    // From native path text — or a file:// URI (RFC 8089, decoded as
    // pathlib.Path.from_uri does). Any other "scheme://" text is rejected.
    constexpr explicit basic_file(std::string_view text, const engine_info* pin = nullptr) : m_pin(pin) {
        assign_text(m_uri, text);
    }

    // ── construction rules (constexpr, so provable) ────────────────────────
    [[nodiscard]] static constexpr bool is_file_scheme(std::string_view sch) noexcept {
        return uri::ascii_lower(sch) == "file";
    }

    // "" when `text` is a plain path or a valid file URI; otherwise the reason it is rejected.
    [[nodiscard]] static constexpr std::string problem(std::string_view text) {
        const std::string_view sch = uri::scheme_of(text);
        if (sch.empty()) return "";
        if (is_file_scheme(sch)) {
            uri mapped;
            file_uri_into(mapped, text);
            if (!Flavour::is_absolute(mapped)) return "URI is not absolute: '" + std::string(text) + "'";
            return "";
        }
        const std::string_view rest = text.substr(sch.size() + 1);
        const bool drive_letter = Flavour::name == "windows" && sch.size() == 1;
        if (rest.starts_with("//") && !drive_letter) {
            return "unsupported URI scheme '" + std::string(sch) + "' in '" + std::string(text) +
                   "' (only file:// URIs are accepted)";
        }
        return "";
    }

    // Decode a file URI (RFC 8089) into `u` as a native path value.
    static constexpr void file_uri_into(uri& u, std::string_view text) {
        const uri parsed = uri::parse(text);
        const std::string native = Flavour::from_uri_text(parsed);
        Flavour::parse_into(u, native);
    }

    // Assign path text or a file URI to `u`; throws std::invalid_argument (ValueError) when rejected.
    static constexpr void assign_text(uri& u, std::string_view text) {
        const std::string why = problem(text);
        if (!why.empty()) throw std::invalid_argument(why);
        const std::string_view sch = uri::scheme_of(text);
        if (!sch.empty() && is_file_scheme(sch)) {
            file_uri_into(u, text);
        } else {
            Flavour::parse_into(u, text);
        }
    }

    // ── value ──────────────────────────────────────────────────────────────
    [[nodiscard]] constexpr const uri& value() const noexcept { return m_uri; }
    [[nodiscard]] constexpr const engine_info* pinned() const noexcept { return m_pin; }

    // os.PathLike: the native path text, in pathlib's normalised spelling.
    [[nodiscard]] constexpr std::string fspath() const { return Flavour::render(m_uri); }

    // Value equality (the pin does not take part); ordering is element-wise.
    [[nodiscard]] constexpr bool operator==(const basic_file& o) const noexcept { return m_uri == o.m_uri; }
    [[nodiscard]] constexpr bool operator<(const basic_file& o) const noexcept {
        return std::tie(m_uri.has_authority, m_uri.authority, m_uri.absolute, m_uri.segments) <
               std::tie(o.m_uri.has_authority, o.m_uri.authority, o.m_uri.absolute, o.m_uri.segments);
    }

    // ── name components (pathlib parity: case preserved) ──────────────────
    [[nodiscard]] constexpr bool has_name() const noexcept { return m_uri.segments.size() > anchor_count(); }
    [[nodiscard]] constexpr std::string name() const { return has_name() ? m_uri.segments.back() : std::string(); }

    [[nodiscard]] constexpr std::string stem() const {
        const std::string n = name();
        const std::size_t dot = n.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot == n.size() - 1) return n;
        return n.substr(0, dot);
    }

    // Final extension including the dot (".gz"); "" if none. A leading-dot
    // name (".bashrc") and a trailing dot ("a.") have no suffix, as in pathlib.
    [[nodiscard]] constexpr std::string suffix() const {
        const std::string n = name();
        const std::size_t dot = n.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot == n.size() - 1) return "";
        return n.substr(dot);
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
    [[nodiscard]] constexpr std::string ext_key() const { return detail::ascii_lower(suffix()); }

    // pathlib's parts: the anchor ("/", "C:\", "\\server\share\"), then the components.
    [[nodiscard]] constexpr std::vector<std::string> parts() const {
        std::vector<std::string> out;
        if (const std::string a = Flavour::anchor(m_uri); !a.empty()) out.push_back(a);
        for (std::size_t i = anchor_count(); i < m_uri.segments.size(); ++i) out.push_back(m_uri.segments[i]);
        return out;
    }

    [[nodiscard]] constexpr bool is_absolute() const noexcept { return Flavour::is_absolute(m_uri); }

    // ── URI form ───────────────────────────────────────────────────────────
    // An absolute path renders per RFC 3986/8089 (file:///a%20b, file://host/share/x).
    // A relative path keeps the legacy "file://<path>" spelling (it has no RFC form).
    [[nodiscard]] constexpr std::string as_uri() const {
        if (is_absolute()) {
            uri u = m_uri;
            Flavour::make_file_uri(u);
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
        std::string out = "file(\"" + as_uri() + "\"";
        if (m_pin) out += ", engine=" + std::string(m_pin->label);
        return out + ")";
    }

    // ── composition (pathlib-style) ────────────────────────────────────────
    [[nodiscard]] constexpr basic_file joined(std::string_view other) const {
        basic_file out = *this;
        const uri rhs = Flavour::parse(other);
        Flavour::join_into(out.m_uri, rhs);
        return out;
    }
    [[nodiscard]] constexpr basic_file rjoined(std::string_view other) const {   // other / self
        basic_file out(other, m_pin);
        Flavour::join_into(out.m_uri, m_uri);
        return out;
    }

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
            if (!cur.has_name() && !Flavour::is_anchored(cur.m_uri)) break;
            out.push_back(cur);
        }
        return out;
    }

    // pathlib's rule: a name is any non-empty component without a separator
    // that is not "." (".." is allowed, as in pathlib).
    [[nodiscard]] constexpr basic_file with_name(std::string_view n) const {
        if (n.empty() || n == ".") throw std::invalid_argument("invalid name: '" + std::string(n) + "'");
        for (char c : n) {
            if (Flavour::is_sep(c)) throw std::invalid_argument("invalid name: '" + std::string(n) + "'");
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
    [[nodiscard]] basic_file resolve() const { return from_fs(fs::weakly_canonical(to_fs())); }

    [[nodiscard]] bool exists() const { return fs::exists(to_fs()); }
    [[nodiscard]] bool is_file() const { return fs::is_regular_file(to_fs()); }
    [[nodiscard]] bool is_dir() const { return fs::is_directory(to_fs()); }
    [[nodiscard]] bool is_symlink() const { return fs::is_symlink(to_fs()); }
    [[nodiscard]] std::uintmax_t size() const { return fs::file_size(to_fs()); }

    // Children of this directory, sorted for determinism.
    [[nodiscard]] std::vector<basic_file> iterdir() const {
        std::vector<basic_file> out;
        for (const auto& e : fs::directory_iterator(to_fs())) out.push_back(from_fs(e.path()));
        std::sort(out.begin(), out.end());
        return out;
    }

    // Relative glob: `*` and `?` within a component, `/` separates components,
    // `**` matches any number of directories (and, as the final component,
    // every descendant). Sorted, deduplicated.
    [[nodiscard]] std::vector<basic_file> glob(std::string_view pattern) const {
        if (pattern.empty()) throw std::invalid_argument("glob: empty pattern");
        std::vector<std::string> segs;
        for (std::size_t start = 0; start <= pattern.size();) {
            const std::size_t slash = pattern.find('/', start);
            const std::size_t end = (slash == std::string_view::npos) ? pattern.size() : slash;
            if (end > start) segs.emplace_back(pattern.substr(start, end - start));
            if (slash == std::string_view::npos) break;
            start = slash + 1;
        }
        // A leading '/' is rejected explicitly: on Windows fs::path("/x") is
        // NOT is_absolute() (no drive), yet the pattern is clearly not
        // relative in the caller's intent.
        if (segs.empty() || pattern.front() == '/' || fs::path(pattern).is_absolute()) {
            throw std::invalid_argument("glob: pattern must be relative, got '" +
                                        std::string(pattern) + "'");
        }
        std::vector<basic_file> out;
        glob_walk(to_fs(), segs, 0, out);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // glob("**/" + pattern): the pattern anywhere under this directory.
    [[nodiscard]] std::vector<basic_file> rglob(std::string_view pattern) const {
        return glob("**/" + std::string(pattern));
    }

    // The raw bytes of the file, undecoded (binary, no newline translation).
    [[nodiscard]] std::string read_bytes() const {
        std::ifstream ifs(to_fs(), std::ios::binary);
        if (!ifs) throw std::runtime_error("cannot open file: " + fspath());
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        return buffer.str();
    }

    // Replace the file's contents with `data`, byte for byte (pathlib parity).
    void write_bytes(std::string_view data) const {
        std::ofstream ofs(to_fs(), std::ios::binary | std::ios::trunc);
        if (!ofs) throw std::runtime_error("cannot open file for writing: " + fspath());
        ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!ofs) throw std::runtime_error("write failed: " + fspath());
    }

    // Create this directory (pathlib parity): `parents` creates missing
    // ancestors, `exist_ok` tolerates an existing directory. A non-directory
    // in the way always fails.
    void mkdir(bool parents = false, bool exist_ok = false) const {
        const fs::path p = to_fs();
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            if (exist_ok) return;
            throw std::runtime_error("directory exists: " + fspath());
        }
        if (fs::exists(p, ec)) {
            throw std::runtime_error("not a directory: " + fspath());
        }
        const bool made = parents ? fs::create_directories(p, ec) : fs::create_directory(p, ec);
        if (ec || !made) {
            throw std::runtime_error("cannot create directory " + fspath() +
                                     (ec ? ": " + ec.message() : std::string{}));
        }
    }

private:
    [[nodiscard]] constexpr std::size_t anchor_count() const noexcept { return Flavour::anchor_segments(m_uri); }

    [[nodiscard]] basic_file from_fs(const fs::path& p) const {
        basic_file out;
        out.m_pin = m_pin;
        Flavour::parse_into(out.m_uri, detail::text_from_fs(p));
        return out;
    }

    // Walk one pattern segment at `base`. Iteration errors (permissions,
    // races) are skipped rather than thrown — matching pathlib's tolerance.
    void glob_walk(const fs::path& base, const std::vector<std::string>& segs,
                   std::size_t idx, std::vector<basic_file>& out) const {
        const std::string& seg = segs[idx];
        const bool last = idx + 1 == segs.size();
        std::error_code ec;
        if (seg == "**") {
            if (last) {   // final `**`: every descendant, files and directories
                for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end;
                     it.increment(ec)) {
                    out.push_back(from_fs(it->path()));
                }
                return;
            }
            glob_walk(base, segs, idx + 1, out);   // `**` matching zero directories
            for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code dec;
                if (it->is_directory(dec) && !dec) glob_walk(it->path(), segs, idx + 1, out);
            }
            return;
        }
        for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            if (!detail::glob_match(seg, detail::text_from_fs(it->path().filename()))) continue;
            if (last) {
                out.push_back(from_fs(it->path()));
            } else {
                std::error_code dec;
                if (it->is_directory(dec) && !dec) glob_walk(it->path(), segs, idx + 1, out);
            }
        }
    }

    uri m_uri;
    const engine_info* m_pin{nullptr};   // pinned at construction; nullptr = auto by extension
};

using file = basic_file<native_flavour>;

}  // namespace pygim::pathlike
