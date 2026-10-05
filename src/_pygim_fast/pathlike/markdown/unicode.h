#pragma once
// pathlike/markdown/unicode.h — the Unicode and HTML facts CommonMark needs, as lookups.
//
// Pybind-free and constexpr. The data is tables.h (generated from Python's
// unicodedata and html.entities); this header decodes UTF-8 and answers the
// four questions the parser asks:
//
//     decode(u8"é!", 0)        -> {U+00E9, 2}      one code point and its length
//     is_punctuation(U+0021)   -> true             '!' is in category Po
//     is_whitespace(U+00A0)    -> true             no-break space is Zs
//     fold_label("Foo  ẞar")   -> "foo ssar"       case fold, inner whitespace collapsed
//     entity("ouml")           -> "ö"              an HTML5 named reference, or ""
//
// Invalid UTF-8 decodes as U+FFFD one byte at a time, so a scan always
// advances; the engines reject invalid UTF-8 before parsing anyway
// (adapter/common.h require_utf8).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "tables.h"

namespace pygim::pathlike::markdown {

/// One decoded code point and how many bytes it took.
struct code_point {
    char32_t cp = 0;
    std::size_t len = 0;
};

/// The code point starting at byte `i` of `s` (U+FFFD, length 1, for a malformed byte).
///     decode("aé", 1) -> {U+00E9, 2}
[[nodiscard]] constexpr code_point decode(std::string_view s, std::size_t i) noexcept {
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    if (c < 0x80) return {c, 1};
    const auto cont = [&](std::size_t k) { return k < s.size() && (b(k) & 0xC0) == 0x80; };
    // Each length keeps only the values it alone may spell: an overlong form
    // (C0 80 for U+0000), a surrogate (ED A0 80) or a value past U+10FFFF is malformed.
    if ((c & 0xE0) == 0xC0 && cont(i + 1)) {
        const auto v = static_cast<char32_t>(((c & 0x1F) << 6) | (b(i + 1) & 0x3F));
        if (v >= 0x80) return {v, 2};
    } else if ((c & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
        const auto v = static_cast<char32_t>(((c & 0x0F) << 12) | ((b(i + 1) & 0x3F) << 6) | (b(i + 2) & 0x3F));
        if (v >= 0x800 && (v < 0xD800 || v > 0xDFFF)) return {v, 3};
    } else if ((c & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        const auto v = static_cast<char32_t>(((c & 0x07) << 18) | ((b(i + 1) & 0x3F) << 12) | ((b(i + 2) & 0x3F) << 6) |
                                             (b(i + 3) & 0x3F));
        if (v >= 0x10000 && v <= 0x10FFFF) return {v, 4};
    }
    return {0xFFFD, 1};
}

/// The code point that ends just before byte `i` (U+000A — a line end — when i == 0).
///     decode_before("aé", 3) -> {U+00E9, 2}
[[nodiscard]] constexpr code_point decode_before(std::string_view s, std::size_t i) noexcept {
    if (i == 0) return {U'\n', 0};
    std::size_t k = i - 1;
    while (k > 0 && i - k < 4 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    const code_point c = decode(s, k);
    return k + c.len == i ? c : code_point{0xFFFD, 1};
}

/// Appends `cp` as UTF-8 (an invalid or zero code point as U+FFFD, as CommonMark requires).
constexpr void encode(char32_t cp, std::string& out) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// ASCII punctuation: the characters a backslash may escape.
[[nodiscard]] constexpr bool is_ascii_punctuation(char c) noexcept {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

/// A Unicode punctuation character: general category P or S (CommonMark 0.31.2).
/// Whether `cp` lies in one of a sorted table's ranges.
template <std::size_t N>
[[nodiscard]] constexpr bool in_ranges(const tables::cp_range (&table)[N], char32_t cp) noexcept {
    const auto* it = std::upper_bound(std::begin(table), std::end(table), cp,
                                      [](char32_t v, const tables::cp_range& r) { return v < r.lo; });
    return it != std::begin(table) && cp <= (it - 1)->hi;
}

[[nodiscard]] constexpr bool is_punctuation(char32_t cp) noexcept {
    if (cp < 0x80) return is_ascii_punctuation(static_cast<char>(cp));
    return in_ranges(tables::punctuation, cp);
}

/// Appends `cp` lower-cased by the full Unicode mapping (İ -> i̇), as str.lower
/// and JavaScript's toLowerCase do — but for Σ, whose form depends on its
/// neighbours (append_lower_at). Not case folding: µ, ſ and ꭰ stay themselves.
constexpr void append_lower(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(cp >= 'A' && cp <= 'Z' ? static_cast<char>(cp + 32) : static_cast<char>(cp));
        return;
    }
    const auto* end = std::end(tables::lowercase);
    const auto* it = std::lower_bound(std::begin(tables::lowercase), end, cp,
                                      [](const tables::fold& f, char32_t v) { return f.cp < v; });
    if (it != end && it->cp == cp) out += it->to;
    else encode(cp, out);
}

/// Appends the code point at byte `i` of `text` lower-cased in its context:
/// a Σ is ς when it ends a word — a cased letter before it and none after it,
/// case-ignorable code points (marks, apostrophes, ...) skipped both ways —
/// Unicode's Final_Sigma, as str.lower and toLowerCase apply it.
constexpr void append_lower_at(std::string_view text, std::size_t i, std::string& out) {
    const code_point c = decode(text, i);
    if (c.cp != 0x3A3) {
        append_lower(c.cp, out);
        return;
    }
    const auto cased_or_stop = [](char32_t cp) { return !in_ranges(tables::case_ignorable, cp); };
    bool before = false;
    for (std::size_t k = i; k > 0;) {
        const code_point p = decode_before(text, k);
        k -= p.len;
        if (cased_or_stop(p.cp)) {
            before = in_ranges(tables::cased, p.cp);
            break;
        }
    }
    bool after = false;
    for (std::size_t k = i + c.len; k < text.size();) {
        const code_point n = decode(text, k);
        k += n.len;
        if (cased_or_stop(n.cp)) {
            after = in_ranges(tables::cased, n.cp);
            break;
        }
    }
    out += before && !after ? "\xCF\x82" : "\xCF\x83";   // ς : σ
}

/// Unicode whitespace: category Zs, tab, line feed, form feed, carriage return.
[[nodiscard]] constexpr bool is_whitespace(char32_t cp) noexcept {
    return std::binary_search(std::begin(tables::whitespace), std::end(tables::whitespace), cp);
}

/// Appends the full case fold of `cp` (itself when it does not fold).
constexpr void append_fold(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(cp >= 'A' && cp <= 'Z' ? static_cast<char>(cp + 32) : static_cast<char>(cp));
        return;
    }
    const auto* end = std::end(tables::casefold);
    const auto* it = std::lower_bound(std::begin(tables::casefold), end, cp,
                                      [](const tables::fold& f, char32_t v) { return f.cp < v; });
    if (it != end && it->cp == cp) {
        out += it->to;
    } else {
        encode(cp, out);
    }
}

/// A link label as CommonMark matches it: case folded, leading and trailing
/// whitespace dropped, every inner run of whitespace one space.
///     fold_label(" Foo\n  BAR ") -> "foo bar"
[[nodiscard]] constexpr std::string fold_label(std::string_view s) {
    std::string out;
    bool space = false;
    for (std::size_t i = 0; i < s.size();) {
        const code_point c = decode(s, i);
        i += c.len;
        if (c.cp == ' ' || c.cp == '\t' || c.cp == '\n' || c.cp == '\r') {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        append_fold(c.cp, out);
    }
    return out;
}

/// The ASCII that NFKD leaves of a non-ASCII code point (`é` -> "e",
/// `ﬁ` -> "fi"), or an empty view when nothing ASCII remains.
[[nodiscard]] constexpr std::string_view ascii_fold(char32_t cp) noexcept {
    const auto* end = std::end(tables::ascii_folds);
    const auto* it = std::lower_bound(std::begin(tables::ascii_folds), end, cp,
                                      [](const tables::ascii_fold& f, char32_t v) { return f.cp < v; });
    return it != end && it->cp == cp ? it->to : std::string_view{};
}

/// The text an HTML5 named character reference stands for (`name` without
/// '&' and ';'), or an empty view when the name is not one.
[[nodiscard]] constexpr std::string_view entity(std::string_view name) noexcept {
    const auto* end = std::end(tables::entities);
    const auto* it = std::lower_bound(std::begin(tables::entities), end, name,
                                      [](const tables::entity& e, std::string_view v) { return e.name < v; });
    return it != end && it->name == name ? it->text : std::string_view{};
}

}  // namespace pygim::pathlike::markdown
