#pragma once
// pathlike/markdown/syntax.h — the small grammars blocks and inlines share.
//
// Link labels, destinations and titles appear both in link reference
// definitions (a block) and in inline links, so their scanners live here,
// with the two text transformations every renderer needs. All pybind-free
// and constexpr; positions are byte offsets into the text being scanned.
//
//     link_label("[foo] x")              -> 5          the bytes of "[foo]", 0 when none
//     link_destination("</my url>)", 0)  -> {"/my%20url", 9}
//     link_title("\"a \\\"b\\\"\" x", 0) -> {"a \"b\"", 8}
//     unescape("\\*a\\* &amp; &#65;")    -> "*a* & A"
//     normalize_uri("/a b?ö")            -> "/a%20b?%C3%B6"
//     escape_html("<a & \"b\">")         -> "&lt;a &amp; &quot;b&quot;&gt;"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "unicode.h"

namespace pygim::pathlike::markdown {

/// `n` in decimal (std::to_string is not constexpr before C++26).
[[nodiscard]] constexpr std::string decimal(std::uint64_t n) {
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('0' + n % 10));
        n /= 10;
    } while (n);
    return s;
}

[[nodiscard]] constexpr bool is_space_or_tab(char c) noexcept { return c == ' ' || c == '\t'; }
[[nodiscard]] constexpr bool is_line_end(char c) noexcept { return c == '\n' || c == '\r'; }
/// ASCII whitespace as CommonMark's grammars use it: space, tab, line feed, line tabulation, form feed, carriage return.
[[nodiscard]] constexpr bool is_ascii_whitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
[[nodiscard]] constexpr bool is_ascii_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] constexpr bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr bool is_ascii_alnum(char c) noexcept { return is_ascii_alpha(c) || is_ascii_digit(c); }
[[nodiscard]] constexpr bool is_hex_digit(char c) noexcept {
    return is_ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
[[nodiscard]] constexpr char ascii_lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

/// `s` without leading and trailing spaces and tabs.
[[nodiscard]] constexpr std::string_view trim_space_tab(std::string_view s) noexcept {
    while (!s.empty() && is_space_or_tab(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space_or_tab(s.back())) s.remove_suffix(1);
    return s;
}
/// `s` without leading and trailing ASCII whitespace.
[[nodiscard]] constexpr std::string_view trim_whitespace(std::string_view s) noexcept {
    while (!s.empty() && is_ascii_whitespace(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_ascii_whitespace(s.back())) s.remove_suffix(1);
    return s;
}

/// An entity or numeric character reference at s[i] ('&'): its text and length.
struct entity_match {
    std::string text;
    std::size_t len = 0;
};

/// `&amp;`, `&#65;`, `&#x41;` at s[i] -> its text; nullopt when there is none
/// there (an unknown name such as `&bogus;` is not a reference and stays text).
[[nodiscard]] constexpr std::optional<entity_match> match_entity(std::string_view s, std::size_t i) {
    if (i + 2 >= s.size() || s[i] != '&') return std::nullopt;
    std::size_t j = i + 1;
    if (s[j] == '#') {
        ++j;
        const bool hex = j < s.size() && (s[j] == 'x' || s[j] == 'X');
        if (hex) ++j;
        const std::size_t first = j, max = hex ? 6 : 7;
        std::uint32_t cp = 0;
        while (j < s.size() && j - first < max && (hex ? is_hex_digit(s[j]) : is_ascii_digit(s[j]))) {
            const char c = s[j];
            const std::uint32_t d = is_ascii_digit(c) ? static_cast<std::uint32_t>(c - '0')
                                                       : static_cast<std::uint32_t>(ascii_lower(c) - 'a' + 10);
            cp = cp * (hex ? 16 : 10) + d;
            ++j;
        }
        if (j == first || j >= s.size() || s[j] != ';') return std::nullopt;
        entity_match m;
        encode(cp, m.text);   // 0, surrogates and out-of-range become U+FFFD
        m.len = j + 1 - i;
        return m;
    }
    if (!is_ascii_alpha(s[j])) return std::nullopt;
    const std::size_t first = j;
    while (j < s.size() && j - first < 32 && is_ascii_alnum(s[j])) ++j;
    if (j - first < 2 || j >= s.size() || s[j] != ';') return std::nullopt;
    const std::string_view text = entity(s.substr(first, j - first));
    if (text.empty()) return std::nullopt;
    return entity_match{std::string(text), j + 1 - i};
}

/// Backslash escapes and entity references resolved (link destinations,
/// titles, info strings).   unescape("\\[x\\] &copy;") -> "[x] ©"
[[nodiscard]] constexpr std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size() && is_ascii_punctuation(s[i + 1])) {
            out.push_back(s[i + 1]);
            i += 2;
        } else if (s[i] == '&') {
            if (auto m = match_entity(s, i)) {
                out += m->text;
                i += m->len;
            } else {
                out.push_back(s[i++]);
            }
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

/// Appends `s` with `&`, `<`, `>` and `"` as entities (and NUL as U+FFFD).
constexpr void escape_html(std::string_view s, std::string& out) {
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\0': out += "\xEF\xBF\xBD"; break;
            default: out.push_back(c);
        }
    }
}
[[nodiscard]] constexpr std::string escape_html(std::string_view s) {
    std::string out;
    escape_html(s, out);
    return out;
}

/// A URL as CommonMark's reference renderer writes it (mdurl's encode):
/// ASCII letters, digits and `;/?:@&=+$,-_.!~*'()#` kept, a valid `%XX`
/// kept, every other byte percent-encoded in upper-case hex.
[[nodiscard]] constexpr std::string normalize_uri(std::string_view s) {
    constexpr std::string_view keep = ";/?:@&=+$,-_.!~*'()#";
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '%' && i + 2 < s.size() && is_hex_digit(s[i + 1]) && is_hex_digit(s[i + 2])) {
            out.append(s.substr(i, 3));
            i += 2;
            continue;
        }
        if (is_ascii_alnum(c) || keep.find(c) != std::string_view::npos) {
            out.push_back(c);
        } else {
            const auto b = static_cast<unsigned char>(c);
            out.push_back('%');
            out.push_back(hex[b >> 4]);
            out.push_back(hex[b & 15]);
        }
    }
    return out;
}

/// The length of a link label `[...]` at s[i] (brackets included), or 0: at
/// most 999 characters inside, no unescaped bracket, not only whitespace is
/// checked by the caller (it folds the label).
[[nodiscard]] constexpr std::size_t link_label(std::string_view s, std::size_t i) noexcept {
    if (i >= s.size() || s[i] != '[') return 0;
    std::size_t j = i + 1;
    while (j < s.size()) {
        const char c = s[j];
        if (c == '\\' && j + 1 < s.size() && is_ascii_punctuation(s[j + 1])) {
            j += 2;
        } else if (c == '[') {
            return 0;
        } else if (c == ']') {
            return j - i - 1 > 999 ? 0 : j + 1 - i;
        } else {
            ++j;
        }
        if (j - i - 1 > 999) return 0;
    }
    return 0;
}

/// A scanned destination or title: the decoded value and the bytes it took.
struct scanned {
    std::string value;
    std::size_t len = 0;
};

/// How deeply a bare link destination may nest parentheses. The spec allows a
/// limit ("at least three levels"); without one, `[a](b[a](b...` rescans the
/// rest of the paragraph at every `]` — quadratic. cmark uses the same bound.
inline constexpr int max_paren_depth = 32;

/// A link destination at s[i]: `<...>` (no line ending, no unescaped `<`/`>`)
/// or a run with balanced parentheses and no space or ASCII control. The
/// value is unescaped and URL-normalised. Empty is allowed only before `)`.
[[nodiscard]] constexpr std::optional<scanned> link_destination(std::string_view s, std::size_t i) {
    if (i < s.size() && s[i] == '<') {
        std::size_t j = i + 1;
        while (j < s.size()) {
            const char c = s[j];
            if (c == '\\' && j + 1 < s.size() && is_ascii_punctuation(s[j + 1])) {
                j += 2;
                continue;
            }
            if (c == '>') return scanned{normalize_uri(unescape(s.substr(i + 1, j - i - 1))), j + 1 - i};
            if (c == '<' || c == '\n' || c == '\r') return std::nullopt;
            ++j;
        }
        return std::nullopt;
    }
    std::size_t j = i;
    int open = 0;
    while (j < s.size()) {
        const char c = s[j];
        if (c == '\\' && j + 1 < s.size() && is_ascii_punctuation(s[j + 1])) {
            j += 2;
        } else if (c == '(') {
            if (++open > max_paren_depth) return std::nullopt;
            ++j;
        } else if (c == ')') {
            if (open < 1) break;
            --open;
            ++j;
        } else if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) {
            break;
        } else {
            ++j;
        }
    }
    if (j == i && !(j < s.size() && s[j] == ')')) return std::nullopt;
    if (open != 0) return std::nullopt;
    return scanned{normalize_uri(unescape(s.substr(i, j - i))), j - i};
}

/// A link title at s[i]: `"..."`, `'...'` or `(...)`, backslash escapes
/// allowed; the value is unescaped.
[[nodiscard]] constexpr std::optional<scanned> link_title(std::string_view s, std::size_t i) {
    if (i >= s.size()) return std::nullopt;
    const char open = s[i];
    const char close = open == '(' ? ')' : open;
    if (open != '"' && open != '\'' && open != '(') return std::nullopt;
    std::size_t j = i + 1;
    while (j < s.size()) {
        const char c = s[j];
        if (c == '\\' && j + 1 < s.size()) {
            j += 2;
            continue;
        }
        if (c == close) return scanned{unescape(s.substr(i + 1, j - i - 1)), j + 1 - i};
        if (open == '(' && c == '(') return std::nullopt;
        ++j;
    }
    return std::nullopt;
}

/// Spaces and tabs, then at most one line ending, then spaces and tabs (the
/// `spnl` of the reference parser): the position after them.
[[nodiscard]] constexpr std::size_t skip_spnl(std::string_view s, std::size_t i) noexcept {
    while (i < s.size() && is_space_or_tab(s[i])) ++i;
    if (i < s.size() && s[i] == '\n') {
        ++i;
        while (i < s.size() && is_space_or_tab(s[i])) ++i;
    }
    return i;
}

// ── HTML tags (inline raw HTML, and HTML block start condition 7) ─────────
// Lengths at s[i], 0 when the form is not there. Whitespace between
// attributes may include line endings, as the reference parser's \s does.

[[nodiscard]] constexpr std::size_t skip_ascii_whitespace(std::string_view s, std::size_t i) noexcept {
    while (i < s.size() && is_ascii_whitespace(s[i])) ++i;
    return i;
}

/// A tag name at s[i]: an ASCII letter, then letters, digits and '-'. Returns the end.
[[nodiscard]] constexpr std::size_t tag_name_end(std::string_view s, std::size_t i) noexcept {
    if (i >= s.size() || !is_ascii_alpha(s[i])) return i;
    ++i;
    while (i < s.size() && (is_ascii_alnum(s[i]) || s[i] == '-')) ++i;
    return i;
}

/// Where a search for the end of an HTML form already failed: a later search
/// for the same end cannot succeed either. One per inline subject, so
/// `<!--<!--<!--...` costs a scan, not a scan per `<` (cmark keeps the same
/// memory). Positions are search starts; npos means "not failed yet".
struct html_misses {
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    std::size_t comment = npos, pi = npos, cdata = npos, declaration = npos, dquote = npos, squote = npos;
};

/// s.find(end, from), answered without scanning when an earlier search from
/// at or before `from` already failed.
[[nodiscard]] constexpr std::size_t find_remembered(std::string_view s, std::string_view end, std::size_t from,
                                                    std::size_t* failed) noexcept {
    if (failed && *failed <= from) return std::string_view::npos;
    const std::size_t at = s.find(end, from);
    if (at == std::string_view::npos && failed && from < *failed) *failed = from;
    return at;
}

/// `<name attr="v" ... />` or `</name >` at s[i].
[[nodiscard]] constexpr std::size_t html_open_or_close_tag(std::string_view s, std::size_t i, html_misses* misses = nullptr) noexcept {
    if (i + 1 >= s.size() || s[i] != '<') return 0;
    if (s[i + 1] == '/') {
        const std::size_t e = tag_name_end(s, i + 2);
        if (e == i + 2) return 0;
        const std::size_t j = skip_ascii_whitespace(s, e);
        return j < s.size() && s[j] == '>' ? j + 1 - i : 0;
    }
    std::size_t j = tag_name_end(s, i + 1);
    if (j == i + 1) return 0;
    for (;;) {   // attributes: whitespace+, name, optional value spec
        const std::size_t ws = skip_ascii_whitespace(s, j);
        if (ws == j || ws >= s.size()) break;
        const char c = s[ws];
        if (!(is_ascii_alpha(c) || c == '_' || c == ':')) break;
        std::size_t k = ws + 1;
        while (k < s.size() && (is_ascii_alnum(s[k]) || s[k] == '_' || s[k] == '.' || s[k] == ':' || s[k] == '-')) ++k;
        j = k;   // the attribute without a value
        std::size_t v = skip_ascii_whitespace(s, k);
        if (v < s.size() && s[v] == '=') {
            v = skip_ascii_whitespace(s, v + 1);
            if (v < s.size() && (s[v] == '"' || s[v] == '\'')) {
                std::size_t* failed = misses ? (s[v] == '"' ? &misses->dquote : &misses->squote) : nullptr;
                const std::size_t close = find_remembered(s, s.substr(v, 1), v + 1, failed);
                if (close != std::string_view::npos) j = close + 1;
            } else {
                std::size_t u = v;
                while (u < s.size()) {
                    const char d = s[u];
                    if (static_cast<unsigned char>(d) <= 0x20 || d == '"' || d == '\'' || d == '=' || d == '<' || d == '>' || d == '`') break;
                    ++u;
                }
                if (u > v) j = u;
            }
        }
    }
    j = skip_ascii_whitespace(s, j);
    if (j < s.size() && s[j] == '/') ++j;
    return j < s.size() && s[j] == '>' ? j + 1 - i : 0;
}

/// Any inline raw HTML form at s[i]: an open or closing tag, a comment
/// (`<!-->`, `<!--->`, `<!-- ... -->`), a processing instruction
/// (`<? ... ?>`), a declaration (`<!X ... >`) or a CDATA section.
[[nodiscard]] constexpr std::size_t html_inline(std::string_view s, std::size_t i, html_misses* misses = nullptr) noexcept {
    if (i + 1 >= s.size() || s[i] != '<') return 0;
    const std::string_view rest = s.substr(i);
    const auto until = [&](std::size_t from, std::string_view end, std::size_t html_misses::*which) -> std::size_t {
        const std::size_t at = find_remembered(s, end, i + from, misses ? &(misses->*which) : nullptr);
        return at == std::string_view::npos ? 0 : at + end.size() - i;
    };
    if (rest.starts_with("<!--")) {
        if (rest.starts_with("<!-->")) return 5;
        if (rest.starts_with("<!--->")) return 6;
        return until(4, "-->", &html_misses::comment);
    }
    if (rest.starts_with("<?")) return until(2, "?>", &html_misses::pi);
    if (rest.starts_with("<![CDATA[")) return until(9, "]]>", &html_misses::cdata);
    if (rest.size() > 2 && rest[1] == '!' && is_ascii_alpha(rest[2])) return until(2, ">", &html_misses::declaration);
    return html_open_or_close_tag(s, i, misses);
}

/// A link reference definition at the start of `s` (a paragraph's text,
/// lines joined by '\n'): the folded label, destination, title, and the
/// bytes consumed (through the end of its last line). nullopt when `s` does
/// not start with one.
struct reference {
    std::string label;
    std::string destination;
    std::string title;
    std::size_t len = 0;
};

[[nodiscard]] constexpr std::optional<reference> link_reference(std::string_view s) {
    const std::size_t label_len = link_label(s, 0);
    if (label_len == 0 || label_len >= s.size() || s[label_len] != ':') return std::nullopt;
    std::string label = fold_label(s.substr(1, label_len - 2));
    if (label.empty()) return std::nullopt;
    std::size_t i = skip_spnl(s, label_len + 1);
    auto dest = link_destination(s, i);
    if (!dest || (dest->len == 0 && !(i < s.size() && s[i] == '<'))) return std::nullopt;
    i += dest->len;
    const std::size_t before_title = i;
    const std::size_t after_spnl = skip_spnl(s, i);
    std::optional<scanned> title;
    if (after_spnl != before_title) title = link_title(s, after_spnl);
    const auto at_line_end = [&](std::size_t k) -> std::optional<std::size_t> {
        while (k < s.size() && is_space_or_tab(s[k])) ++k;
        if (k == s.size()) return k;
        if (s[k] == '\n') return k + 1;
        return std::nullopt;
    };
    std::optional<std::size_t> end;
    if (title) end = at_line_end(after_spnl + title->len);
    if (!end) {
        title.reset();
        end = at_line_end(before_title);
    }
    if (!end) return std::nullopt;
    return reference{std::move(label), std::move(dest->value), title ? std::move(title->value) : std::string{}, *end};
}

}  // namespace pygim::pathlike::markdown
