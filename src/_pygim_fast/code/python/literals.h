#pragma once
// code/python/literals.h — a Constant's value, decoded from the source only when it is asked for.
//
//     std::string v;
//     decode_text(tree, tree[i].value(), v, names);   // a str's value as WTF-8, a bytes value as bytes
//     number_text(tree, i)                            // "0x_ff" -> "0xff": the digits, ready to convert
//
// A string is the pieces it was written in (rows.h): escapes are decoded here — \n, \x41, €,
// \U0001F600, octal, and \N{NAME} through the caller's table of names (Q37: none in C++ yet; the
// adapter asks CPython's) — with line endings as \n, as CPython reads them. A str's value comes
// out as WTF-8: UTF-8 that may hold a lone surrogate ("\ud800" is a valid str literal).
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "../../utils/chars.h"
#include "rows.h"
#include "source.h"
#include "tree.h"

namespace pygim::code::python {

/// Names of characters: none known. What a decode uses when nobody gives it a table.
class no_character_names {
public:
    [[nodiscard]] std::optional<std::uint32_t> operator()(std::string_view) const noexcept { return std::nullopt; }
};

namespace detail {

using chars::append_wtf8;   // utils/chars.h, as source.h's decoding
using chars::hex_value;     // utils/chars.h: the lexer's too

// `span` onto `out` with each \r\n and lone \r read as \n, as CPython reads a source's line ends.
inline void append_lf(std::string& out, std::string_view span) {
    for (std::size_t k = 0; k < span.size(); ++k) {
        if (span[k] != '\r') {
            out.push_back(span[k]);
            continue;
        }
        out.push_back('\n');
        if (k + 1 < span.size() && span[k + 1] == '\n') ++k;
    }
}

// An expression's text as CPython keeps it for f"{x = }" and t"{x}": as written, but for its
// comments, which go up to their line's end; a `#` inside one of its strings stays, and every line
// end reads as \n, a string's own included.
inline void decode_code(std::string_view text, const literal_piece& p, std::string& out) {
    for (std::uint32_t i = p.start(); i < p.end();) {
        const char c = text[i];
        if (c == '#') {
            while (i < p.end() && text[i] != '\n' && text[i] != '\r') ++i;
            continue;
        }
        if (c == '\r') {
            out.push_back('\n');
            i += (i + 1 < p.end() && text[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        if (c != '"' && c != '\'') {
            out.push_back(c);
            ++i;
            continue;
        }
        const bool triple = i + 2 < p.end() && text[i + 1] == c && text[i + 2] == c;
        const std::uint32_t quotes = triple ? 3 : 1;
        std::uint32_t j = i + quotes;
        while (j < p.end() && !(text[j] == c && (!triple || (j + 2 < p.end() && text[j + 1] == c && text[j + 2] == c)))) j += text[j] == '\\' ? 2 : 1;
        j = std::min<std::uint32_t>(j + quotes, p.end());
        append_lf(out, text.substr(i, j - i));   // a string's own \r\n reads as \n too (CPython; it was copied raw)
        i = j;
    }
}

// One piece of a literal decoded onto `out`.
template <class Names>
void decode_piece(std::string_view text, const literal_piece& p, std::string& out, const Names& names) {
    if (p.has(piece_flag::code)) {
        decode_code(text, p, out);
        return;
    }
    const bool bytes = p.has(piece_flag::bytes), braces = p.has(piece_flag::fstring);
    const bool escapes = !p.has(piece_flag::raw) && !p.has(piece_flag::verbatim);
    for (std::uint32_t i = p.start(); i < p.end();) {
        const char c = text[i];
        if (c == '\r') {   // \r\n and a lone \r read as \n
            out.push_back('\n');
            i += (i + 1 < p.end() && text[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        if (braces && (c == '{' || c == '}') && i + 1 < p.end() && text[i + 1] == c) {
            out.push_back(c);
            i += 2;
            continue;
        }
        if (c != '\\' || !escapes) {
            out.push_back(c);
            ++i;
            continue;
        }
        const char e = text[i + 1];
        i += 2;
        switch (e) {
            case '\n': break;
            case '\r': if (i < p.end() && text[i] == '\n') ++i; break;
            case '\\': out.push_back('\\'); break;
            case '\'': out.push_back('\''); break;
            case '"': out.push_back('"'); break;
            case 'a': out.push_back('\a'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'v': out.push_back('\v'); break;
            case 'x': {
                const auto v = static_cast<std::uint32_t>(hex_value(text[i]) * 16 + hex_value(text[i + 1]));
                i += 2;
                if (bytes) out.push_back(static_cast<char>(v));
                else append_wtf8(out, v);
                break;
            }
            case 'u':
            case 'U': {
                if (bytes) {
                    out.push_back('\\');
                    out.push_back(e);
                    break;
                }
                const int n = e == 'u' ? 4 : 8;
                std::uint32_t v = 0;
                for (int k = 0; k < n; ++k) v = v * 16 + static_cast<std::uint32_t>(hex_value(text[i + k]));
                i += static_cast<std::uint32_t>(n);
                append_wtf8(out, v);
                break;
            }
            case 'N': {
                if (bytes) {
                    out.push_back('\\');
                    out.push_back('N');
                    break;
                }
                const std::uint32_t close = static_cast<std::uint32_t>(text.find('}', i));
                const std::string_view name = text.substr(i + 1, close - i - 1);
                i = close + 1;
                if (const std::optional<std::uint32_t> cp = names(name)) {
                    append_wtf8(out, *cp);
                } else {   // no table knows it: kept as written
                    out.append("\\N{");
                    out.append(name);
                    out.push_back('}');
                }
                break;
            }
            default:
                if (e >= '0' && e <= '7') {   // up to three octal digits
                    std::uint32_t v = static_cast<std::uint32_t>(e - '0');
                    for (int k = 0; k < 2 && i < p.end() && text[i] >= '0' && text[i] <= '7'; ++k, ++i) v = v * 8 + static_cast<std::uint32_t>(text[i] - '0');
                    if (bytes) out.push_back(static_cast<char>(v & 0xFF));
                    else append_wtf8(out, v);
                } else {   // not an escape: both characters stay
                    out.push_back('\\');
                    i -= 1;
                }
                break;
        }
    }
}

}  // namespace detail

/// The value of literal `index` of `tree` appended to `out`: WTF-8 for a str, the bytes for bytes.
template <class Names = no_character_names>
void decode_text(const syntax_tree& tree, std::uint32_t index, std::string& out, const Names& names = Names{}) {
    const literal& l = tree.literal_at(index);
    for (std::uint32_t k = 0; k < l.count(); ++k) detail::decode_piece(tree.text(), tree.piece_at(l.first() + k), out, names);
}

/// A number Constant's digits without underscores ("1_000.5j" -> "1000.5j"), ready for int() or float().
[[nodiscard]] inline std::string number_text(const syntax_tree& tree, std::uint32_t i) {
    const std::string_view written = tree.text().substr(tree[i].start(), tree[i].end() - tree[i].start());
    std::string out;
    out.reserve(written.size());
    for (char c : written) {
        if (c != '_') out.push_back(c);
    }
    return out;
}

}  // namespace pygim::code::python
