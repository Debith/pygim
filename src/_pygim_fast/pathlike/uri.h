#pragma once
// pathlike/uri.h — RFC 3986 URIs as constexpr values.
//
// The generic syntax (§3)   scheme ":" hier-part [ "?" query ] [ "#" fragment ]
// is decomposed as in Appendix B, recomposed as in §5.3 and normalised as in
// §6.2. Path segments are stored DECODED — percent-encoding is a transport
// detail of the text form — and kept exactly as written: dot-segments and
// empty segments survive parsing. RFC normalisation (remove_dot_segments,
// case folding of scheme and host) is an explicit operation, never implicit,
// because the path algebra built on top of this (core.h, pathlib parity)
// keeps ".." and collapses "a//b" by its own rules, not the RFC's.
//
// Everything here is constexpr and pybind-free, so the RFC behaviour is
// proven at compile time in tests/static/pathlike_core_proofs.cpp.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::pathlike {

struct uri {
    std::string scheme;                  // "" = a relative reference
    bool has_authority = false;          // "//" was present; the authority itself may be empty (file:///x)
    std::string authority;               // [ userinfo "@" ] host [ ":" port ], raw
    bool absolute = false;               // the path begins with "/"
    std::vector<std::string> segments;   // decoded path segments, as written
    bool has_query = false;
    std::string query;                   // raw text
    bool has_fragment = false;
    std::string fragment;                // raw text

    constexpr bool operator==(const uri&) const = default;

    // ── character classes (§2) ────────────────────────────────────────────
    [[nodiscard]] static constexpr bool is_alpha(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    [[nodiscard]] static constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
    [[nodiscard]] static constexpr bool is_unreserved(char c) noexcept {
        return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~';
    }
    [[nodiscard]] static constexpr bool is_sub_delim(char c) noexcept {
        for (char d : std::string_view{"!$&'()*+,;="}) {
            if (c == d) return true;
        }
        return false;
    }
    // pchar = unreserved / pct-encoded / sub-delims / ":" / "@": what a segment may carry unencoded.
    [[nodiscard]] static constexpr bool is_pchar(char c) noexcept {
        return is_unreserved(c) || is_sub_delim(c) || c == ':' || c == '@';
    }

    // ── percent-encoding (§2.1) ───────────────────────────────────────────
    // Encodes every byte that is not a pchar (so "/" inside a segment is encoded too).
    [[nodiscard]] static constexpr std::string percent_encode(std::string_view s) {
        constexpr std::string_view hex = "0123456789ABCDEF";
        std::string out;
        for (char c : s) {
            if (is_pchar(c)) {
                out += c;
                continue;
            }
            const unsigned char u = static_cast<unsigned char>(c);
            out += '%';
            out += hex[u >> 4];
            out += hex[u & 15];
        }
        return out;
    }

    [[nodiscard]] static constexpr int hex_value(char c) noexcept {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    }

    // Decodes "%XX" triplets; malformed escapes are kept literally (as urllib does).
    [[nodiscard]] static constexpr std::string percent_decode(std::string_view s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) {
                const int hi = hex_value(s[i + 1]), lo = hex_value(s[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out += static_cast<char>(hi * 16 + lo);
                    i += 2;
                    continue;
                }
            }
            out += s[i];
        }
        return out;
    }

    // ── scheme (§3.1): ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":" ─────
    // The scheme of `s`, or "" when `s` has none.
    [[nodiscard]] static constexpr std::string_view scheme_of(std::string_view s) noexcept {
        if (s.empty() || !is_alpha(s.front())) return {};
        for (std::size_t i = 1; i < s.size(); ++i) {
            const char c = s[i];
            if (c == ':') return s.substr(0, i);
            if (!(is_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.')) return {};
        }
        return {};
    }

    // ASCII case folding — the only case folding a URI (or a path key) needs.
    [[nodiscard]] static constexpr char to_lower(char c) noexcept {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    [[nodiscard]] static constexpr char to_upper(char c) noexcept {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    [[nodiscard]] static constexpr std::string ascii_lower(std::string_view s) {
        std::string out(s);
        for (char& c : out) c = to_lower(c);
        return out;
    }
    [[nodiscard]] static constexpr std::string ascii_upper(std::string_view s) {
        std::string out(s);
        for (char& c : out) c = to_upper(c);
        return out;
    }

    // ── decomposition (Appendix B) ────────────────────────────────────────
    [[nodiscard]] static constexpr uri parse(std::string_view s) {
        constexpr auto npos = std::string_view::npos;
        uri u;
        if (const std::string_view sch = scheme_of(s); !sch.empty()) {
            u.scheme = std::string(sch);
            s.remove_prefix(sch.size() + 1);
        }
        if (const std::size_t hash = s.find('#'); hash != npos) {
            u.has_fragment = true;
            u.fragment = std::string(s.substr(hash + 1));
            s = s.substr(0, hash);
        }
        if (const std::size_t q = s.find('?'); q != npos) {
            u.has_query = true;
            u.query = std::string(s.substr(q + 1));
            s = s.substr(0, q);
        }
        if (s.starts_with("//")) {
            s.remove_prefix(2);
            const std::size_t slash = s.find('/');
            u.has_authority = true;
            u.authority = std::string(s.substr(0, slash));
            s = slash == npos ? std::string_view{} : s.substr(slash);
        }
        if (s.starts_with('/')) {
            u.absolute = true;
            s.remove_prefix(1);
            if (s.empty()) return u;   // "/" alone: rooted, no segments
        }
        if (s.empty()) return u;
        std::size_t start = 0;
        while (true) {
            const std::size_t slash = s.find('/', start);
            u.segments.push_back(percent_decode(s.substr(start, slash == npos ? npos : slash - start)));
            if (slash == npos) break;
            start = slash + 1;
        }
        return u;
    }

    // Appends segments[from..] to `out`, `sep`-joined, percent-encoded when
    // `encode` — the one segment-joining loop behind path(), render() and the
    // strategies' native rendering (core.h).
    constexpr void append_segments(std::string& out, std::size_t from, char sep, bool encode) const {
        for (std::size_t i = from; i < segments.size(); ++i) {
            if (i > from) out += sep;
            if (encode) out += percent_encode(segments[i]);
            else out += segments[i];
        }
    }

    // The decoded path text: "/" + segments joined by "/" (or without the
    // leading "/" for a relative path).
    [[nodiscard]] constexpr std::string path() const {
        std::string out;
        if (absolute) out += '/';
        append_segments(out, 0, '/', false);
        return out;
    }

    // ── recomposition (§5.3) ──────────────────────────────────────────────
    [[nodiscard]] constexpr std::string render() const {
        std::string out;
        if (!scheme.empty()) {
            out += scheme;
            out += ':';
        }
        if (has_authority) {
            out += "//";
            out += authority;
        }
        if (absolute) out += '/';
        append_segments(out, 0, '/', true);
        if (has_query) {
            out += '?';
            out += query;
        }
        if (has_fragment) {
            out += '#';
            out += fragment;
        }
        return out;
    }

    // ── normalisation (§6.2.2), explicit only ─────────────────────────────
    // remove_dot_segments (§5.2.4) on a segment list: "." vanishes, ".." pops;
    // either as the LAST segment leaves a trailing empty segment ("a/." -> "a/").
    [[nodiscard]] static constexpr std::vector<std::string> remove_dot_segments(const std::vector<std::string>& in) {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const bool last = i + 1 == in.size();
            if (in[i] == ".") {
                if (last) out.emplace_back();
                continue;
            }
            if (in[i] == "..") {
                if (!out.empty()) out.pop_back();
                if (last) out.emplace_back();
                continue;
            }
            out.push_back(in[i]);
        }
        return out;
    }

    // Scheme and host are case-insensitive (§6.2.2.1); the userinfo and port are not.
    [[nodiscard]] constexpr uri normalized() const {
        uri u = *this;
        u.scheme = ascii_lower(u.scheme);
        if (u.has_authority) {
            const std::size_t at = u.authority.find('@');
            const std::size_t host_start = at == std::string::npos ? 0 : at + 1;
            std::size_t host_end = u.authority.size();
            if (host_start < u.authority.size() && u.authority[host_start] == '[') {   // IPv6 literal
                const std::size_t close = u.authority.find(']', host_start);
                host_end = close == std::string::npos ? u.authority.size() : close + 1;
            } else {
                const std::size_t colon = u.authority.find(':', host_start);
                if (colon != std::string::npos) host_end = colon;
            }
            for (std::size_t i = host_start; i < host_end; ++i) u.authority[i] = to_lower(u.authority[i]);
        }
        u.segments = remove_dot_segments(u.segments);
        return u;
    }
};

}  // namespace pygim::pathlike
