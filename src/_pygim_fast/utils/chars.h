#pragma once
// utils/chars.h — the byte classes the source readers share: code/python/lexer.h and
// similarity/cpp_source.h read identifiers and numbers by the same bytes.
//
//     starts_identifier('_') -> true     starts_identifier('7') -> false    starts_identifier('\xc3') -> true
//     continues_identifier('7') -> true  is_hex_digit('F') -> true          is_digit('a') -> false
//     hex_value('B') -> 11
//     is_blank('\t') -> true             is_line_space('\r') -> true        is_line_space('\n') -> false
//     to_lower('Q') -> 'q'               lower("Windows-1252") -> "windows-1252"     upper(".py") -> ".PY"
//
// A byte from 0x80 is part of a UTF-8 sequence: it may start or continue an identifier, and the
// reader decodes it (Python checks XID_Start / XID_Continue) or takes it as written (C++).
//
// Case is ASCII case only: what a URI scheme, a host, a file extension or a codec name folds. A
// byte from 0x80 is left as it is, and nothing depends on the locale (std::tolower does).
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace pygim::chars {

[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
static_assert(is_digit('7') && !is_digit('a'));   // 7.5: a number starts

[[nodiscard]] constexpr bool is_hex_digit(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static_assert(is_hex_digit('F') && !is_hex_digit('g'));   // \xFF
/// An ASCII letter (no locale, no byte from 0x80): what a URI scheme starts with, a drive letter is.
[[nodiscard]] constexpr bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static_assert(is_alpha('q') && is_alpha('Z') && !is_alpha('1') && !is_alpha('\xe9'));
/// An ASCII letter or digit (CPython's Py_ISALNUM: no locale, no byte from 0x80).
[[nodiscard]] constexpr bool is_alnum(char c) noexcept { return is_digit(c) || is_alpha(c); }
static_assert(is_alnum('7') && is_alnum('Q') && !is_alnum('-') && !is_alnum('\xe9'));

/// A hex digit's value, or -1 for any other byte.
[[nodiscard]] constexpr int hex_value(char c) noexcept {
    return is_digit(c) ? c - '0' : ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') ? (c | 0x20) - 'a' + 10 : -1;
}
static_assert(hex_value('7') == 7 && hex_value('B') == 11 && hex_value('g') == -1);   // \x7B

/// An ASCII letter, '_', or a byte of a UTF-8 sequence.
[[nodiscard]] constexpr bool starts_identifier(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
static_assert(starts_identifier('_') && !starts_identifier('7') && starts_identifier('\xc3'));   // _x, not 7x; é starts one

[[nodiscard]] constexpr bool continues_identifier(char c) noexcept { return starts_identifier(c) || is_digit(c); }
static_assert(continues_identifier('7') && !continues_identifier('.'));   // x7, not x.

/// A space or a tab: what separates words on a line.
[[nodiscard]] constexpr bool is_blank(char c) noexcept { return c == ' ' || c == '\t'; }
static_assert(is_blank('\t') && !is_blank('\n'));   // `#  if`: the directive's name after blanks

/// Whitespace that ends no line: a blank, a carriage return (a CRLF line's), a form feed, a vertical tab.
[[nodiscard]] constexpr bool is_line_space(char c) noexcept { return is_blank(c) || c == '\r' || c == '\f' || c == '\v'; }
static_assert(is_line_space('\r') && !is_line_space('\n'));   // a CRLF line's \r is skipped, its \n counted

/// `c` moved from one ASCII case to the other: a letter of the alphabet that starts at `From` to the
/// same letter of the one that starts at `To`; any other byte as it is.
template <char From, char To>
[[nodiscard]] constexpr char ascii_case(char c) noexcept {
    return (c >= From && c <= From + ('z' - 'a')) ? static_cast<char>(c - From + To) : c;
}
static_assert(ascii_case<'A', 'a'>('Z') == 'z' && ascii_case<'A', 'a'>('[') == '[');   // the alphabet's last letter, and the byte after it

[[nodiscard]] constexpr char to_lower(char c) noexcept { return ascii_case<'A', 'a'>(c); }
static_assert(to_lower('Q') == 'q' && to_lower('q') == 'q' && to_lower('\xc3') == '\xc3');   // a URI's host, byte by byte

[[nodiscard]] constexpr char to_upper(char c) noexcept { return ascii_case<'a', 'A'>(c); }
static_assert(to_upper('q') == 'Q' && to_upper('7') == '7');

/// `s` with each byte moved by `to` (to_lower, to_upper).
template <class Case>
[[nodiscard]] constexpr std::string with_case(std::string_view s, Case to) {
    std::string out(s);
    for (char& c : out) c = to(c);
    return out;
}
static_assert(with_case("Ab", to_upper) == "AB");

[[nodiscard]] constexpr std::string lower(std::string_view s) { return with_case(s, to_lower); }
static_assert(lower("Windows-1252") == "windows-1252");   // a codec name, a scheme, a file extension

[[nodiscard]] constexpr std::string upper(std::string_view s) { return with_case(s, to_upper); }
static_assert(upper(".py") == ".PY");   // engine_list's proof that an extension's case does not matter

/// Whether `a` and `b` are one text but for the case of ASCII letters, compared without a copy.
[[nodiscard]] constexpr bool same_folded(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](char x, char y) { return to_lower(x) == to_lower(y); });
}
static_assert(same_folded(".PY", ".py") && !same_folded(".py", ".pyi") && !same_folded(".\xc3", ".\xe3"));   // a byte past ASCII as it is

/// How many bytes a code point's UTF-8 takes, a surrogate's too: what append_wtf8 appends.
[[nodiscard]] constexpr std::size_t wtf8_length(std::uint32_t cp) noexcept { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }
static_assert(wtf8_length(0x41) == 1 && wtf8_length(0xE9) == 2 && wtf8_length(0x20AC) == 3 && wtf8_length(0xD800) == 3 && wtf8_length(0x1F600) == 4);

/// A code point's UTF-8 bytes appended to `out` (a std::string, or one with an allocator of its
/// own), a surrogate's too (WTF-8: what a str holding a lone surrogate is written as).
template <class String>
constexpr void append_wtf8(String& out, std::uint32_t cp) {
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
static_assert([] {
    std::string s;
    for (std::uint32_t cp : {0x41u, 0xE9u, 0x20ACu, 0x1F600u}) append_wtf8(s, cp);
    return s == "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
}());

}  // namespace pygim::chars
