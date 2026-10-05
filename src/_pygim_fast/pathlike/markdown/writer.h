#pragma once
// pathlike/markdown/writer.h — writing markdown: escaping plain text, and the blocks a generator needs.
//
// Pybind-free and constexpr. One rule for every builder: the text you pass
// IS markdown (a cell may hold `**bold**`), and `escape()` makes plain text
// safe to pass. Each builder handles only what would break its structure —
// a `|` in a table cell, a run of backticks inside a code block — so a
// builder's output always parses back as the block it built
// (tests/unittests/test_pathlike_markdown.py round-trips them).
//
//     escape("a*b* [c]")                       -> "a\\*b\\* \\[c\\]"
//     heading(2, "Usage")                      -> "## Usage\n"
//     code("x = `1`\n", "py")                  -> "```py\nx = `1`\n```\n"
//     code("``` inside\n", "")                 -> "````\n``` inside\n````\n"
//     table({"k", "v"}, {{"a|b", "1"}}, {})    -> "| k   | v   |\n| --- | --- |\n| a\\|b | 1   |\n"
//     items({"one", "two"}, true, 1)           -> "1. one\n2. two\n"
//     quote("a\n\nb")                          -> "> a\n>\n> b\n"
//     join({"# T\n", "text"})                  -> "# T\n\ntext\n"
//
// Widths are counted in code points, so a table's columns line up in a
// fixed-width view for most scripts (wide East Asian characters excepted).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "syntax.h"
#include "tree.h"
#include "unicode.h"

namespace pygim::pathlike::markdown::write {

/// Plain text made safe as markdown inline content: the characters that can
/// start inline markup are backslash-escaped everywhere (`\ ` * _ [ ] < | ~ #`),
/// the ones that only start a block at the beginning of a line are escaped
/// there (`> + - =`, the `.`/`)` after digits), and `&` where it would read
/// as an entity reference. Line breaks are kept.
[[nodiscard]] constexpr std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    bool line_start = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n') {
            out.push_back(c);
            line_start = true;
            continue;
        }
        if (line_start) {
            line_start = false;
            if (c == '>' || c == '+' || c == '-' || c == '=') {
                out.push_back('\\');
                out.push_back(c);
                continue;
            }
            if (is_ascii_digit(c)) {   // "1. x" would start an ordered list
                std::size_t j = i;
                while (j < text.size() && is_ascii_digit(text[j])) ++j;
                if (j < text.size() && (text[j] == '.' || text[j] == ')')) {
                    out.append(text.substr(i, j - i));
                    out.push_back('\\');
                    out.push_back(text[j]);
                    i = j;
                    continue;
                }
            }
        }
        switch (c) {
            case '\\': case '`': case '*': case '_': case '[': case ']': case '<': case '|': case '~': case '#':
                out.push_back('\\');
                out.push_back(c);
                break;
            case '&':
                if (match_entity(text, i)) out.push_back('\\');
                out.push_back(c);
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

/// An ATX heading: `level` 1-6 marks, the text on one line (line breaks become spaces).
[[nodiscard]] constexpr std::string heading(int level, std::string_view text) {
    if (level < 1 || level > 6) {
        throw std::invalid_argument("markdown heading level must be 1-6, got " + (level < 0 ? "-" + decimal(static_cast<std::uint64_t>(-static_cast<long long>(level))) : decimal(static_cast<std::uint64_t>(level))));
    }
    std::string out(static_cast<std::size_t>(level), '#');
    out.push_back(' ');
    for (char c : trim_whitespace(text)) out.push_back(c == '\n' || c == '\r' ? ' ' : c);
    out.push_back('\n');
    return out;
}

/// The longest run of `c` in `s`.
[[nodiscard]] constexpr std::size_t longest_run(std::string_view s, char c) noexcept {
    std::size_t best = 0, run = 0;
    for (char x : s) {
        run = x == c ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

/// A fenced code block. The fence is backticks (tildes when the info string
/// holds a backtick), one longer than any run of that character in the code,
/// so no line of the code can close it.
[[nodiscard]] constexpr std::string code(std::string_view text, std::string_view info) {
    const char ch = info.find('`') != std::string_view::npos ? '~' : '`';
    const std::string fence(std::max<std::size_t>(3, longest_run(text, ch) + 1), ch);
    std::string out = fence;
    for (char c : trim_whitespace(info)) out.push_back(c == '\n' || c == '\r' ? ' ' : c);
    out.push_back('\n');
    out += text;
    if (!text.empty() && text.back() != '\n') out.push_back('\n');
    out += fence;
    out.push_back('\n');
    return out;
}

/// The width of `s` in code points.
[[nodiscard]] constexpr std::size_t width(std::string_view s) noexcept {
    std::size_t n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

/// A cell's text made safe inside a row: an unescaped `|` escaped, a line break as `<br>`.
[[nodiscard]] constexpr std::string cell(std::string_view text) {
    std::string out;
    std::size_t backslashes = 0;
    for (char c : trim_whitespace(text)) {
        if (c == '\n') {
            out += "<br>";
            backslashes = 0;
            continue;
        }
        if (c == '\r') continue;
        if (c == '|' && backslashes % 2 == 0) out.push_back('\\');
        backslashes = c == '\\' ? backslashes + 1 : 0;
        out.push_back(c);
    }
    return out;
}

/// A GFM table: the header row, the delimiter row (alignment colons from
/// `aligns`, `none` for columns it does not cover) and the body rows. Rows
/// shorter than the widest are padded with empty cells; every column is
/// padded to its widest cell, so the source reads as a grid.
[[nodiscard]] constexpr std::string table(const std::vector<std::string>& header,
                                          const std::vector<std::vector<std::string>>& rows,
                                          const std::vector<align>& aligns) {
    std::size_t columns = header.size();
    for (const auto& r : rows) columns = std::max(columns, r.size());
    if (columns == 0) throw std::invalid_argument("markdown table needs at least one column");
    std::vector<std::vector<std::string>> grid;
    grid.reserve(rows.size() + 1);
    const auto add = [&](const std::vector<std::string>& r) {
        std::vector<std::string> row;
        row.reserve(columns);
        for (std::size_t c = 0; c < columns; ++c) row.push_back(c < r.size() ? cell(r[c]) : std::string{});
        grid.push_back(std::move(row));
    };
    add(header);
    for (const auto& r : rows) add(r);
    std::vector<std::size_t> widths(columns, 3);
    for (const auto& row : grid) {
        for (std::size_t c = 0; c < columns; ++c) widths[c] = std::max(widths[c], width(row[c]));
    }
    const auto a = [&](std::size_t c) { return c < aligns.size() ? aligns[c] : align::none; };
    const auto line = [&](const std::vector<std::string>& row) {
        std::string out = "|";
        for (std::size_t c = 0; c < columns; ++c) {
            const std::size_t pad = widths[c] - width(row[c]);
            const std::size_t before = a(c) == align::right ? pad : a(c) == align::center ? pad / 2 : 0;
            out.push_back(' ');
            out.append(before, ' ');
            out += row[c];
            out.append(pad - before, ' ');
            out += " |";
        }
        out.push_back('\n');
        return out;
    };
    std::string out = line(grid[0]);
    out += "|";
    for (std::size_t c = 0; c < columns; ++c) {
        const align x = a(c);
        std::string d(widths[c], '-');
        if (x == align::left || x == align::center) d.front() = ':';
        if (x == align::right || x == align::center) d.back() = ':';
        out += " " + d + " |";
    }
    out.push_back('\n');
    for (std::size_t r = 1; r < grid.size(); ++r) out += line(grid[r]);
    return out;
}

/// A bullet list, or a numbered one from `start`. An item's continuation
/// lines are indented under its first character, so a multi-line item stays one item.
[[nodiscard]] constexpr std::string items(const std::vector<std::string>& texts, bool ordered, std::uint32_t start) {
    std::string out;
    std::uint32_t n = start;
    for (const std::string& t : texts) {
        const std::string marker = ordered ? decimal(n++) + ". " : std::string("- ");
        out += marker;
        bool first = true;
        std::string_view rest = trim_whitespace(t);
        for (;;) {
            const std::size_t e = rest.find('\n');
            const std::string_view line = rest.substr(0, e);
            if (!first && !trim_space_tab(line).empty()) out.append(marker.size(), ' ');
            out += line;
            out.push_back('\n');
            first = false;
            if (e == std::string_view::npos) break;
            rest = rest.substr(e + 1);
        }
    }
    return out;
}

/// A block quote: every line prefixed with `> ` (a blank line with `>`).
[[nodiscard]] constexpr std::string quote(std::string_view text) {
    std::string out;
    std::string_view rest = trim_whitespace(text);
    for (;;) {
        const std::size_t e = rest.find('\n');
        const std::string_view line = rest.substr(0, e);
        if (trim_space_tab(line).empty()) out += ">";
        else (out += "> ") += line;
        out.push_back('\n');
        if (e == std::string_view::npos) break;
        rest = rest.substr(e + 1);
    }
    return out;
}

/// Front matter around an already serialised body: `---` for YAML, `+++` for TOML.
[[nodiscard]] constexpr std::string front_matter(std::string_view body, char marker) {
    const std::string fence(3, marker == '+' ? '+' : '-');
    std::string out = fence + "\n";
    out += body;
    if (!body.empty() && body.back() != '\n') out.push_back('\n');
    out += fence + "\n";
    return out;
}

/// Blocks joined into a document: one blank line between them, empty ones
/// dropped, one line ending at the end.
[[nodiscard]] constexpr std::string join(const std::vector<std::string>& blocks) {
    std::string out;
    for (const std::string& b : blocks) {
        std::string_view v = b;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.remove_suffix(1);
        if (v.empty()) continue;
        if (!out.empty()) out += "\n\n";
        out += v;
    }
    if (!out.empty()) out.push_back('\n');
    return out;
}

}  // namespace pygim::pathlike::markdown::write
