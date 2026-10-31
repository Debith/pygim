#pragma once
// pathlike/path_rules.h — pathlib's rules for a path's text, per family of systems: POSIX's and
// Windows', as PurePosixPath and PureWindowsPath have them — separators, drives, UNC hosts, roots,
// whether a glob's names differ by case. Both are compiled on every system, so either is proven and
// tested on any (docs/design/platform_boundary.md, D77); native_strategy is the running system's,
// chosen by platform::native, not by an #if.
//
// CORE layer: pybind-free.

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include "../platform/native.h"
#include "../utils/chars.h"
#include "uri.h"

namespace pygim::pathlike {

namespace detail {
// One plain component under Strategy: no separator, not empty, not "."
// (a join drops it), not a drive spelling. What `p / other` may answer with a
// child row instead of the full join algebra. ".." IS a plain component, as
// in pathlib.
template <class Strategy>
[[nodiscard]] constexpr bool plain_component(std::string_view s) noexcept {
    if (s.empty() || s == ".") return false;
    if (s.size() >= 2 && s[1] == ':') return false;
    for (const char c : s) {
        if (Strategy::is_sep(c)) return false;
    }
    return true;
}

// pathlib's stem and suffix of a final component: the last dot splits them
// unless it is the first or the last character (".bashrc", "a." have no suffix).
[[nodiscard]] constexpr std::string_view suffix_of(std::string_view n) noexcept {
    const std::size_t dot = n.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot == n.size() - 1) return {};
    return n.substr(dot);
}
[[nodiscard]] constexpr std::string_view stem_of(std::string_view n) noexcept {
    const std::size_t dot = n.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot == n.size() - 1) return n;
    return n.substr(0, dot);
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

// The host of a file URI that names a remote machine: pathlib.Path.from_uri
// treats an empty authority and "localhost" as the local machine.
[[nodiscard]] constexpr std::string_view remote_host(const uri& u) noexcept {
    if (!u.has_authority || u.authority.empty() || u.authority == "localhost") return {};
    return u.authority;
}
}  // namespace detail

// What every strategy shares, expressed once over the strategy's own
// anchor()/anchor_segments()/sep/parse_into(): the by-value parse, pathlib's
// str() rendering, and the RFC file-URI form. (A strategy is stateless, so
// this is CRTP over static members only.)
template <class Strategy>
struct strategy_base {
    [[nodiscard]] static constexpr uri parse(std::string_view s) {
        uri u;
        Strategy::parse_into(u, s);
        return u;
    }

    // pathlib's str(): the anchor, then the components joined; "." when empty.
    [[nodiscard]] static constexpr std::string render(const uri& u) {
        std::string out = Strategy::anchor(u);
        u.append_segments(out, Strategy::anchor_segments(u), Strategy::sep, false);
        return out.empty() ? std::string(".") : out;
    }

    // pathlib's order of two paths' texts, as render() writes them: PurePath.__lt__ compares
    // str(path).split(sep), part by part, so a separator sorts before any character of a part and
    // the shorter of two equal runs comes first. "/b" < "a" (an absolute path's first part is
    // empty), "a/b" < "a-b" ('/' is no '-'), "a" < "a/b". Bytes compare as code points do, UTF-8
    // keeping their order. Case counts, as it does for == (PureWindowsPath folds it: not here).
    [[nodiscard]] static constexpr std::strong_ordering text_order(std::string_view a, std::string_view b) noexcept {
        const std::size_t shorter = a.size() < b.size() ? a.size() : b.size();
        for (std::size_t i = 0; i < shorter; ++i) {
            const bool a_sep = a[i] == Strategy::sep, b_sep = b[i] == Strategy::sep;
            if (a_sep != b_sep) return a_sep ? std::strong_ordering::less : std::strong_ordering::greater;
            if (a[i] != b[i]) return static_cast<unsigned char>(a[i]) <=> static_cast<unsigned char>(b[i]);
        }
        return a.size() <=> b.size();
    }

    // The RFC form of an absolute path, in place: file:///a/b — an authority is
    // present (empty unless the path already names a host, as a UNC share does).
    static constexpr void make_file_uri(uri& u) {
        u.scheme = "file";
        if (!u.has_authority) {
            u.has_authority = true;
            u.authority.clear();
        }
    }

    // The parse as a stream: sink.anchor(absolute, has_authority, authority,
    // anchor_segments) once, then sink.segment(text) per segment — exactly
    // what parse_into() yields, without building a uri when a strategy
    // provides a direct tokeniser (posix_strategy does; this generic one
    // serves the rest). path_table.h consumes it.
    template <class Sink>
    static constexpr void tokenise(std::string_view s, Sink& sink) {
        uri u;
        Strategy::parse_into(u, s);
        sink.anchor(u.absolute, u.has_authority, u.authority, Strategy::anchor_segments(u));
        for (const std::string& seg : u.segments) sink.segment(seg);
    }
};

// ── Strategies: native text <-> uri value, pathlib's rules ───────────────────
// A strategy is a stateless policy: parse() maps a native path string onto the
// uri model, render() maps it back (pathlib's str()), and the anchor helpers
// say which leading segments are the drive/share that name() and parent()
// must never consume. from_uri_text() applies pathlib.Path.from_uri's rules
// to a decoded RFC 8089 file URI.

// POSIX: "/" separates; the root is "/" — or "//", which POSIX reserves for an
// implementation-defined meaning and pathlib therefore preserves (exactly two
// leading slashes; three or more collapse to "/"). Represented as the
// absolute flag plus a leading EMPTY segment, which is also what RFC 3986
// says the path "//x" is.
struct posix_strategy : strategy_base<posix_strategy> {
    static constexpr std::string_view name = "posix";
    static constexpr char sep = '/';
    static constexpr bool case_sensitive = true;   // a glob's default: POSIX names differ by case
    static constexpr bool remote_hosts = false;   // file://host/x has no local meaning: rejected, as pathlib.Path.from_uri (3.14) does
    [[nodiscard]] static constexpr bool is_sep(char c) noexcept { return c == '/'; }

    // Fills `u` in place. (Constant evaluation on GCC 13/14 mishandles a
    // string-holding struct returned by value straight into a member, so every
    // strategy operation mutates in place; parse() is the by-value convenience.)
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

    // parse_into() as a stream, allocation-free (proven equal to parse_into
    // in tests/static/pathlike_core_proofs.cpp).
    template <class Sink>
    static constexpr void tokenise(std::string_view s, Sink& sink) {
        std::size_t slashes = 0;
        while (slashes < s.size() && s[slashes] == '/') ++slashes;
        sink.anchor(slashes > 0, false, std::string_view{}, slashes == 2 ? 1 : 0);
        if (slashes == 2) sink.segment(std::string_view{});   // the "//" root
        s.remove_prefix(slashes);
        std::size_t start = 0;
        while (start <= s.size()) {
            std::size_t end = start;
            while (end < s.size() && s[end] != '/') ++end;
            const std::string_view seg = s.substr(start, end - start);
            if (!seg.empty() && seg != ".") sink.segment(seg);
            if (end == s.size()) break;
            start = end + 1;
        }
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

    // pathlib's join, in place: an absolute `other` replaces the whole path.
    static constexpr void join_into(uri& base, const uri& other) {
        if (other.absolute) {
            base = other;
            return;
        }
        base.segments.insert(base.segments.end(), other.segments.begin(), other.segments.end());
    }

    // RFC 8089 -> native text (pathlib.Path.from_uri): an empty or "localhost"
    // authority is dropped; a remote host was refused by problem() before this.
    [[nodiscard]] static constexpr std::string from_uri_text(const uri& u) { return u.path(); }
};

// Windows: "\" and "/" both separate. The anchor is a drive ("C:") or a UNC
// share ("\\server\share") plus the root "\". A drive is stored as the first
// segment ("C:", as RFC 8089 writes file:///C:/x) and a UNC host as the
// authority (file://server/share/x); the share is the first segment.
struct windows_strategy : strategy_base<windows_strategy> {
    static constexpr std::string_view name = "windows";
    static constexpr char sep = '\\';
    static constexpr bool case_sensitive = false;   // a glob's default: Windows' names do not
    static constexpr bool remote_hosts = true;   // file://host/share/x is the UNC path \\host\share\x
    [[nodiscard]] static constexpr bool is_sep(char c) noexcept { return c == '\\' || c == '/'; }
    [[nodiscard]] static constexpr bool is_drive(std::string_view seg) noexcept {
        return seg.size() == 2 && chars::is_alpha(seg[0]) && seg[1] == ':';
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
        if (s.size() >= 2 && chars::is_alpha(s[0]) && s[1] == ':') {   // drive
            u.segments.emplace_back(s.substr(0, 2));
            s.remove_prefix(2);
        }
        if (!s.empty() && is_sep(s.front())) u.absolute = true;
        while (!s.empty() && is_sep(s.front())) s.remove_prefix(1);
        detail::append_components(u, s, is_sep);
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

    [[nodiscard]] static constexpr bool same_drive(const uri& a, const uri& b) noexcept {
        if (!has_drive(a) || !has_drive(b)) return false;
        return chars::to_lower(a.segments.front()[0]) == chars::to_lower(b.segments.front()[0]);
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
        if (const std::string_view host = detail::remote_host(u); !host.empty()) text = "\\\\" + std::string(host);
        std::string p = u.path();
        if (text.empty() && p.size() >= 3 && p[0] == '/' && chars::is_alpha(p[1]) && (p[2] == ':' || p[2] == '|')) {
            p = p.substr(1);
            p[1] = ':';
        }
        return text + p;
    }
};

/// The running system's rules.
using native_strategy = std::conditional_t<platform::native == platform::family::windows, windows_strategy, posix_strategy>;
static_assert(std::is_same_v<native_strategy, windows_strategy> || std::is_same_v<native_strategy, posix_strategy>);

// pathlib's order, as sorted([PurePosixPath(s) ...]) and PureWindowsPath give it.
static_assert(posix_strategy::text_order("/b", "a") < 0 && posix_strategy::text_order("a/b", "a-b") < 0 &&
              posix_strategy::text_order("a", "a/b") < 0 && posix_strategy::text_order("a.b", "a/b") > 0 &&
              posix_strategy::text_order("/", "/a") < 0 && posix_strategy::text_order("x/\xc3\xa4", "x/z") > 0 &&
              posix_strategy::text_order("a/b", "a/b") == 0 && posix_strategy::text_order("B", "a") < 0);
static_assert(windows_strategy::text_order("C:\\a\\b", "C:\\a-b") < 0 && windows_strategy::text_order("\\b", "a") < 0);

}  // namespace pygim::pathlike
