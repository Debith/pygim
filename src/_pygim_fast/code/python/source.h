#pragma once
// code/python/source.h — a file's bytes made ready to tokenize: the encoding it declares, its text
// as UTF-8 with no byte-order mark, and where each line starts. Also the error every stage raises.
//
//     source_text src = source_text::from_bytes(std::move(bytes));   // throws syntax_error
//     src.text()            -> the UTF-8 text; text().data()[text().size()] == '\0'
//     src.position_of(offset) -> {line 1.., column in bytes}, as CPython's ast counts them
//
// The encoding (PEP 263, Q36): UTF-8 by default, with or without a byte-order mark; a coding
// cookie in the first two lines names a codec as Python's registry finds it, by its name or any
// alias (codec_tables.gen.h). UTF-8 and ASCII are checked; Latin-1 and every ASCII-compatible
// single-byte codec (cp1252, koi8-r, iso8859-*, mac-*: 62 of them) are converted to UTF-8 through
// their tables. A multi-byte encoding (shift_jis, gbk) is refused as an error of the file; a codec
// that is no text encoding (base64) and a name the registry does not know as CPython refuses them.
//
// Lines end at \n, \r\n or a lone \r, as CPython's tokenizer reads them.
//
// The text and its lines are in the memory its bytes were read into (an arena_string's): a
// corpus's in its workers' chunks, anything else's on the heap. Each is made once, at its size, as
// an arena gives nothing back until it dies: the lines are counted before they are written, a
// single-byte codec's UTF-8 measured before it is decoded.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "../../utils/byte_scan.h"
#include "../../utils/chars.h"
#include "../../utils/chunk_arena.h"
#include "codec_tables.gen.h"

namespace pygim::code::python {

/// A file that is not Python: where, as a byte offset into its text, and why. An error found while
/// the text is decoded — before there is a text to read lines from — carries its line and column
/// too (line 0: none given, the reader's text says).
/// Where in its source an error is, as CPython's SyntaxError tells it: in the text (a line and a
/// column), on no line at all — a null byte, refused before the text is made (lineno None) —
/// before the text, where a codec could not decode a byte or the coding cookie names no codec it
/// has (lineno 0, offset -1), or on the first line with no column, a byte-order mark with another
/// encoding than UTF-8 (lineno 1, offset 0).
enum class error_place : std::uint8_t { in_text, no_line, before_text, first_line };

class syntax_error : public std::exception {
public:
    syntax_error(std::string message, std::uint32_t offset) : m_message(std::move(message)), m_offset(offset) {}
    syntax_error(std::string message, std::uint32_t offset, std::uint32_t line, std::uint32_t column)
        : m_message(std::move(message)), m_offset(offset), m_line(line), m_column(column) {}
    /// An error with no place in the text (`place` no_line or before_text).
    syntax_error(std::string message, error_place place) : m_message(std::move(message)), m_offset(0), m_place(place) {}
    [[nodiscard]] const char* what() const noexcept override { return m_message.c_str(); }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
    [[nodiscard]] std::uint32_t offset() const noexcept { return m_offset; }
    [[nodiscard]] std::uint32_t line() const noexcept { return m_line; }       ///< from 1; 0 when not given
    [[nodiscard]] std::uint32_t column() const noexcept { return m_column; }   ///< in UTF-8 bytes from 0, with line()
    [[nodiscard]] error_place place() const noexcept { return m_place; }

private:
    std::string m_message;
    std::uint32_t m_offset;
    std::uint32_t m_line = 0;
    std::uint32_t m_column = 0;
    error_place m_place = error_place::in_text;
};

/// A place in a file: its line from 1, its column in UTF-8 bytes from 0 (ast's col_offset).
class position {
public:
    constexpr position(std::uint32_t line, std::uint32_t column) noexcept : m_line(line), m_column(column) {}
    [[nodiscard]] constexpr std::uint32_t line() const noexcept { return m_line; }
    [[nodiscard]] constexpr std::uint32_t column() const noexcept { return m_column; }

private:
    std::uint32_t m_line;
    std::uint32_t m_column;
};
static_assert(position(3, 4).line() == 3 && position(3, 4).column() == 4);   // syntax_tree::position_of's answer

namespace detail {

// Where a UTF-8 text stops being valid UTF-8 (surrogates and overlong forms refused), or npos.
[[nodiscard]] inline std::size_t first_invalid_utf8(std::string_view s) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        if (p[i] < 0x80) {   // an ASCII run, 64 bytes a step
            i = byte_scan::first_non_ascii(s, i);
            continue;
        }
        const unsigned char c = p[i];
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
        else return i;
        if (i + len > n) return i;
        for (std::size_t k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return i;
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if ((len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return i;
        i += len;
    }
    return std::string_view::npos;
}

// Why the UTF-8 at `i` is none, as CPython's decoder says it, and how many bytes it names: the
// first byte starts no sequence (that byte), or the next one continues none (every byte before
// it). The text's end reads as the newline CPython decodes the source with.
class utf8_fault {
public:
    constexpr utf8_fault(std::string_view why, std::size_t length) noexcept : m_why(why), m_length(length) {}
    [[nodiscard]] constexpr std::string_view why() const noexcept { return m_why; }
    [[nodiscard]] constexpr std::size_t length() const noexcept { return m_length; }

private:
    std::string_view m_why;
    std::size_t m_length;
};
[[nodiscard]] constexpr utf8_fault utf8_fault_at(std::string_view s, std::size_t i) noexcept {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0xC2 || c > 0xF4) return {"invalid start byte", 1};
    const std::size_t len = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    std::size_t k = 1;
    for (; k < len && i + k < s.size(); ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        // the second byte's range rules out overlong forms (E0, F0), surrogates (ED) and past U+10FFFF (F4)
        const unsigned char lo = k == 1 && c == 0xE0 ? 0xA0 : k == 1 && c == 0xF0 ? 0x90 : 0x80;
        const unsigned char hi = k == 1 && c == 0xED ? 0x9F : k == 1 && c == 0xF4 ? 0x8F : 0xBF;
        if (b < lo || b > hi) break;
    }
    return {"invalid continuation byte", k};
}
static_assert(utf8_fault_at("\xff", 0).why() == "invalid start byte" && utf8_fault_at("a\xc3", 1).length() == 1 &&
              utf8_fault_at("\xe2\x82", 0).length() == 2 && utf8_fault_at("\xed\xa0\x80", 0).length() == 1 &&
              utf8_fault_at("\xe0\x80\x80", 0).why() == "invalid continuation byte");

// A byte as CPython's decoding errors write it: 0xe9.
[[nodiscard]] inline std::string byte_hex(unsigned char c) {
    constexpr std::string_view digits = "0123456789abcdef";
    return {'0', 'x', digits[c >> 4], digits[c & 15]};
}

// What a codec says of the `length` bytes from `position` of the file it cannot decode (CPython's
// UnicodeDecodeError, raised before there is a text): one byte by its value, more by their range.
[[nodiscard]] inline syntax_error undecodable(std::string_view codec, std::string_view bytes, std::size_t position, std::size_t length, std::string_view why) {
    const std::string what = length == 1 ? "byte " + byte_hex(static_cast<unsigned char>(bytes[position])) + " in position " + std::to_string(position)
                                         : "bytes in position " + std::to_string(position) + "-" + std::to_string(position + length - 1);
    return syntax_error("'" + std::string(codec) + "' codec can't decode " + what + ": " + std::string(why), error_place::before_text);
}

// A codec's name as Python's codec registry looks it up: lower case, each run of characters
// other than letters, digits and '.' as one '_', none at either end ("Windows-1252" -> "windows_1252").
[[nodiscard]] inline std::string registry_key(std::string_view name) {
    std::string key;
    bool gap = false;
    for (char c : name) {
        const char lower = chars::to_lower(c);
        if (chars::is_alnum(lower) || lower == '.') {
            if (gap && !key.empty()) key.push_back('_');
            key.push_back(lower);
            gap = false;
        } else {
            gap = true;
        }
    }
    return key;
}

// What CPython's tokenizer calls the encoding a cookie names (get_normal_name): "utf-8" and
// "iso-8859-1" by their spellings (lower case, '_' as '-', a "-..." suffix allowed), any other
// name as written. Only "utf-8" may go with a byte-order mark.
[[nodiscard]] inline std::string tokenizer_name(std::string_view cookie) {
    std::string n;
    for (char c : cookie.substr(0, 12)) n.push_back(c == '_' ? '-' : chars::to_lower(c));   // CPython reads 12 bytes
    auto starts = [&](std::string_view p) { return n.starts_with(p); };
    if (n == "utf-8" || starts("utf-8-")) return "utf-8";
    for (std::string_view l : {"latin-1", "iso-8859-1", "iso-latin-1"}) {
        if (n == l || starts(std::string(l) + "-")) return "iso-8859-1";
    }
    return std::string(cookie);
}

// The codec Python's registry finds for an encoding's name, as encodings.search_function finds
// it: the name in the registry's spelling as an alias or a codec module, else, each '.' read as
// '_', as an alias. None: an unknown encoding.
[[nodiscard]] inline std::optional<codecs::codec> codec_named(std::string_view name) {
    std::string key = registry_key(name);
    if (const std::optional<codecs::codec_name> found = codecs::name_of(key)) return found->get();
    std::ranges::replace(key, '.', '_');
    if (const std::optional<codecs::codec_name> found = codecs::name_of(key); found && found->alias()) return found->get();
    return std::nullopt;
}

// The encoding a line declares (`# -*- coding: latin-1 -*-`), or empty: a comment, with
// "coding" then ':' or '=' then the name, as PEP 263's regular expression reads it.
[[nodiscard]] inline std::string_view cookie_of(std::string_view line) noexcept {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\f')) ++i;
    if (i == line.size() || line[i] != '#') return {};
    for (std::size_t at = line.find("coding", i); at != std::string_view::npos; at = line.find("coding", at + 1)) {
        std::size_t j = at + 6;
        if (j >= line.size() || (line[j] != ':' && line[j] != '=')) continue;
        ++j;
        while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
        const std::size_t name_start = j;
        while (j < line.size() && (chars::is_alnum(line[j]) || line[j] == '-' || line[j] == '_' || line[j] == '.')) ++j;
        if (j > name_start) return line.substr(name_start, j - name_start);
    }
    return {};
}

// The first line of `text` from `start`, without its line ending.
[[nodiscard]] inline std::string_view line_from(std::string_view text, std::size_t start) noexcept {
    std::size_t end = start;
    while (end < text.size() && text[end] != '\n' && text[end] != '\r') ++end;
    return text.substr(start, end - start);
}

// Where the line after the one starting at `start` begins (text.size() past the last).
[[nodiscard]] inline std::size_t next_line(std::string_view text, std::size_t start) noexcept {
    std::size_t i = start;
    while (i < text.size() && text[i] != '\n' && text[i] != '\r') ++i;
    if (i < text.size()) i += (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ? 2 : 1;
    return i;
}

}  // namespace detail

/// A file's text as the tokenizer reads it: UTF-8, its byte-order mark left out, a NUL after its
/// end, and the offset where each line starts; both in the memory the bytes were in.
class source_text {
public:
    using lines = arena_vector<std::uint32_t>;   // the offset each line starts at, in order

    /// The text of a file's bytes, in the encoding they declare; a file that cannot be decoded is
    /// a syntax_error, as compile() raises it.
    [[nodiscard]] static source_text from_bytes(arena_string bytes) {
        source_text s;
        std::size_t skip = 0;
        const bool bom = bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0;
        if (bom) skip = 3;
        const std::string_view all(bytes);
        std::string encoding = "utf-8";   // as the tokenizer calls it
        std::size_t line = skip;
        for (int k = 0; k < 2 && line < all.size(); ++k) {   // the cookie: on the first or second line
            const std::string_view text = detail::line_from(all, line);
            const std::string_view cookie = detail::cookie_of(text);
            if (!cookie.empty()) {
                encoding = detail::tokenizer_name(cookie);
                break;
            }
            const std::size_t first = text.find_first_not_of(" \t\f");
            if (first != std::string_view::npos && text[first] != '#') break;   // code on line 1: no cookie on line 2
            line = detail::next_line(all, line);
        }
        if (bom && encoding != "utf-8") throw syntax_error("encoding problem: " + encoding + " with BOM", error_place::first_line);
        const std::optional<codecs::codec> codec = detail::codec_named(encoding);
        if (!codec) throw syntax_error("unknown encoding: " + encoding, error_place::before_text);
        switch (codec->form()) {
            case codecs::form::utf8:
            case codecs::form::ascii: {
                const bool ascii = codec->form() == codecs::form::ascii;
                s.m_text = std::move(bytes);
                if (skip) s.m_text.erase(0, skip);
                const std::size_t bad = ascii ? first_non_ascii(s.m_text) : detail::first_invalid_utf8(s.m_text);
                if (bad == std::string_view::npos) break;
                const auto c = static_cast<unsigned char>(s.m_text[bad]);
                if (ascii) throw detail::undecodable("ascii", s.m_text, bad, 1, "ordinal not in range(128)");
                if (encoding != "utf-8") {   // an alias (utf8): the codec's error
                    const detail::utf8_fault f = detail::utf8_fault_at(s.m_text, bad);
                    throw detail::undecodable("utf-8", s.m_text, bad, f.length(), f.why());
                }
                s.index_lines();   // the tokenizer's own UTF-8: CPython names the bad byte's line
                const auto offset = static_cast<std::uint32_t>(bad);
                const position at = s.position_of(offset);
                throw syntax_error("Non-UTF-8 code starting with '\\x" + detail::byte_hex(c).substr(2) + "' on line " + std::to_string(at.line()) +
                                       ", but no encoding declared; see https://peps.python.org/pep-0263/ for details",
                                   offset, at.line(), at.column());
            }
            case codecs::form::table:
                s.m_text = single_byte_to_utf8(all.substr(skip), codecs::upper_half[codec->table()], bytes.get_allocator());
                break;
            case codecs::form::not_text:
                throw syntax_error("'" + encoding + "' is not a text encoding; use codecs.decode() to handle arbitrary codecs", error_place::before_text);
            case codecs::form::unsupported:
                throw syntax_error("encoding '" + encoding + "' is not supported yet (utf-8, ascii and the single-byte codecs are)", error_place::before_text);
        }
        if (s.m_text.size() >= 0xFFFFFFF0u) throw syntax_error("file too large: offsets are 32-bit", 0);
        const std::size_t nul = s.m_text.find('\0');
        if (nul != std::string::npos) throw syntax_error("source code cannot contain null bytes", error_place::no_line);
        s.index_lines();
        return s;
    }

    /// A text that is already decoded (a str's UTF-8): no byte-order mark or coding cookie is
    /// looked for, as compile() ignores them in a str.
    [[nodiscard]] static source_text from_text(arena_string utf8) {
        source_text s;
        s.m_text = std::move(utf8);
        if (s.m_text.size() >= 0xFFFFFFF0u) throw syntax_error("file too large: offsets are 32-bit", 0);
        const std::size_t nul = s.m_text.find('\0');
        if (nul != std::string::npos) throw syntax_error("source code cannot contain null bytes", error_place::no_line);
        s.index_lines();
        return s;
    }

    /// The text, UTF-8: what every offset counts in. A NUL follows its last byte.
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }
    /// The line and column of a byte offset.
    [[nodiscard]] position position_of(std::uint32_t offset) const noexcept { return position_in(m_lines, offset); }
    /// The arena the text and its lines are in; null on the heap.
    [[nodiscard]] chunk_arena* arena() const noexcept { return m_text.get_allocator().arena(); }
    /// The line and column of a byte offset, given the starts of the lines.
    [[nodiscard]] static position position_in(std::span<const std::uint32_t> lines, std::uint32_t offset) noexcept {
        const auto it = std::upper_bound(lines.begin(), lines.end(), offset);
        const auto line = static_cast<std::uint32_t>(it - lines.begin());   // 1-based: the count of starts at or before
        return {line, offset - lines[line - 1]};
    }

    /// Gives the text away (a tree keeps it, so its literals can be decoded later).
    [[nodiscard]] arena_string release() && noexcept { return std::move(m_text); }
    [[nodiscard]] lines release_lines() && noexcept { return std::move(m_lines); }

private:
    source_text() = default;

    // Bytes of a single-byte codec as UTF-8, in `memory`: ASCII as it is, 0x80-0xFF through the
    // codec's table. Counted first, so the text is made once at its size.
    [[nodiscard]] static arena_string single_byte_to_utf8(std::string_view in, const std::array<std::uint16_t, 128>& upper, const arena_allocator<char>& memory) {
        std::size_t size = 0;
        for (const char b : in) {
            const auto c = static_cast<unsigned char>(b);
            size += c < 0x80 ? 1 : chars::wtf8_length(upper[c - 0x80]);   // an undefined byte is refused below, before the room matters
        }
        arena_string out(memory);
        out.reserve(size);
        for (std::size_t i = 0; i < in.size(); ++i) {
            const auto c = static_cast<unsigned char>(in[i]);
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                continue;
            }
            const std::uint32_t cp = upper[c - 0x80];
            if (cp == codecs::undefined) throw detail::undecodable("charmap", in, i, 1, "character maps to <undefined>");
            chars::append_wtf8(out, cp);
        }
        return out;
    }
    [[nodiscard]] static std::size_t first_non_ascii(std::string_view s) noexcept {
        const std::size_t at = byte_scan::first_non_ascii(s);
        return at == s.size() ? std::string_view::npos : at;
    }

    // The lines, in the text's memory, counted first so they are made once at their size.
    void index_lines() {
        std::size_t count = 1;
        for_each_line_start([&](std::uint32_t) { ++count; });
        m_lines = lines(arena_allocator<std::uint32_t>(m_text.get_allocator()));
        m_lines.reserve(count);
        m_lines.push_back(0);
        for_each_line_start([&](std::uint32_t at) { m_lines.push_back(at); });
    }
    // Each offset a line starts at after the first: past a \n, a \r\n or a lone \r.
    template <class F>
    void for_each_line_start(F&& f) const {
        const std::string_view t = m_text;
        byte_scan::for_each_line_end(t, [&](std::size_t i) {
            if (t[i] == '\r' && i + 1 < t.size() && t[i + 1] == '\n') return;   // \r\n ends one line, at its \n
            f(static_cast<std::uint32_t>(i + 1));
        });
    }

    arena_string m_text;
    lines m_lines;
};

/// Where `e` happened in the text: the line and column decoding gave it, else its offset among the
/// lines of `src` (the text it was found in), else line 1 at that offset; none when the error has
/// no place in the text (error_place).
[[nodiscard]] inline std::optional<position> position_of(const syntax_error& e, const source_text* src) noexcept {
    if (e.place() != error_place::in_text) return std::nullopt;
    if (e.line() != 0) return position(e.line(), e.column());
    return src != nullptr ? src->position_of(e.offset()) : position(1, e.offset());
}

}  // namespace pygim::code::python
