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

/// One line's text, escaped from its first character as a line start: the
/// characters that can start inline markup are backslash-escaped everywhere
/// (`\ ` * _ [ ] < | ~ #`), those that only start a block at the beginning
/// of a line are escaped there (`> + - = :`, the `.`/`)` after digits — `:`
/// so no line reads as a table's delimiter row), `&` where it would read as
/// an entity reference, and a carriage return becomes `&#13;`.
constexpr void escape_line(std::string_view line, std::string& out) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (i == 0) {
            if (c == '>' || c == '+' || c == '-' || c == '=' || c == ':') {
                out.push_back('\\');
                out.push_back(c);
                continue;
            }
            if (is_ascii_digit(c)) {   // "1. x" would start an ordered list
                std::size_t j = i;
                while (j < line.size() && is_ascii_digit(line[j])) ++j;
                if (j < line.size() && (line[j] == '.' || line[j] == ')')) {
                    out.append(line.substr(i, j - i));
                    out.push_back('\\');
                    out.push_back(line[j]);
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
                if (match_entity(line, i)) out.push_back('\\');
                out.push_back(c);
                break;
            case '\r':
                out += "&#13;";
                break;
            default:
                out.push_back(c);
        }
    }
}

/// Plain text made safe as markdown inline content, so that parsing it back
/// gives the text exactly (escape_line says what is escaped). Whitespace a
/// paragraph would strip or read as indentation — spaces and tabs at the
/// start or the end of a line — is written as character references (`&#32;`,
/// `&#9;`), and a carriage return as `&#13;`, so `" > q"` stays text and
/// `"a\r\nb"` reads back with its CR. Line feeds are kept as line breaks,
/// except after a line's trailing whitespace, where the line feed is `&#10;`.
[[nodiscard]] constexpr std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    const auto reference = [&](char c) { out += c == ' ' ? "&#32;" : "&#9;"; };
    for (std::size_t pos = 0;;) {
        std::size_t end = text.find('\n', pos);
        const bool last = end == std::string_view::npos;
        if (last) end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        std::size_t lead = 0;
        while (lead < line.size() && is_space_or_tab(line[lead])) ++lead;
        std::size_t trail = line.size();
        while (trail > lead && is_space_or_tab(line[trail - 1])) --trail;
        for (std::size_t i = 0; i < lead; ++i) reference(line[i]);
        escape_line(line.substr(lead, trail - lead), out);
        for (std::size_t i = trail; i < line.size(); ++i) reference(line[i]);
        if (last) break;
        // A line break drops the spaces before it, decoded ones too (CommonMark 6.7), so
        // after a line ending in whitespace — a whitespace-only line too — the break itself
        // is a reference: no line ends there.
        out += !line.empty() && is_space_or_tab(line.back()) ? "&#10;" : "\n";
        pos = end + 1;
    }
    return out;
}

/// The lines of `text`, split at LF, CRLF or a lone CR (as the parser reads them).
[[nodiscard]] constexpr std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    for (std::size_t pos = 0;;) {
        const auto [end, next] = line_from(text, pos);
        out.push_back(text.substr(pos, end - pos));
        if (next == end) break;   // the last line had no line ending
        pos = next;
        if (pos == text.size()) break;
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
/// so no line of the code can close it. A CR in the code is a line ending, as
/// it is anywhere in markdown, so it reads back as a line feed.
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
    bool prev_cr = false;
    for (char c : trim_whitespace(text)) {
        if (c == '\n' || c == '\r') {   // LF, CRLF and a lone CR are each one line break
            if (!(c == '\n' && prev_cr)) out += "<br>";
            prev_cr = c == '\r';
            backslashes = 0;
            continue;
        }
        prev_cr = false;
        if (c == '|' && backslashes % 2 == 0) out.push_back('\\');
        backslashes = c == '\\' ? backslashes + 1 : 0;
        out.push_back(c);
    }
    return out;
}

/// A GFM table: the header row, the delimiter row (alignment colons from
/// `aligns`: none, or one per column) and the body rows. A row shorter than
/// the header is padded with empty cells; a longer one is refused, since a
/// reader drops the cells past the header's. Every column is padded to its
/// widest cell, so the source reads as a grid.
[[nodiscard]] constexpr std::string table(const std::vector<std::string>& header,
                                          const std::vector<std::vector<std::string>>& rows,
                                          const std::vector<align>& aligns) {
    const std::size_t columns = header.size();
    if (columns == 0) throw std::invalid_argument("markdown table needs at least one column");
    for (std::size_t k = 0; k < rows.size(); ++k) {   // GFM drops the cells past the header's: refuse, not lose them
        if (rows[k].size() > columns) {
            throw std::invalid_argument("table: row " + decimal(k + 1) + " has " + decimal(rows[k].size()) +
                                        " cells, the header " + decimal(columns) + " (a reader drops the rest)");
        }
    }
    if (!aligns.empty() && aligns.size() != columns) {
        throw std::invalid_argument("table: align has " + decimal(aligns.size()) + " values for " + decimal(columns) + " columns");
    }
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
///
/// An ordered list's numbers have at most nine digits (CommonMark 5.2: a
/// longer one is not a list marker, and the items would merge), so `start`
/// and the last item's number must lie in 0-999999999 (std::invalid_argument).
[[nodiscard]] constexpr std::string items(const std::vector<std::string>& texts, bool ordered, std::int64_t start) {
    constexpr std::int64_t most = 999'999'999;
    const auto last = start + static_cast<std::int64_t>(texts.empty() ? 0 : texts.size() - 1);
    if (ordered && (start < 0 || last > most)) {
        const auto signed_decimal = [](std::int64_t v) {
            return v < 0 ? "-" + decimal(static_cast<std::uint64_t>(-v)) : decimal(static_cast<std::uint64_t>(v));
        };
        throw std::invalid_argument("bullets: an ordered list's numbers have at most 9 digits (0-999999999); start=" +
                                    signed_decimal(start) + " with " + decimal(texts.size()) + " items ends at " +
                                    signed_decimal(last));
    }
    std::string out;
    std::uint64_t n = ordered ? static_cast<std::uint64_t>(start) : 0;
    for (const std::string& t : texts) {
        const std::string marker = ordered ? decimal(n++) + ". " : std::string("- ");
        out += marker;
        bool first = true;
        for (const std::string_view line : lines_of(trim_whitespace(t))) {
            if (!first && !trim_space_tab(line).empty()) out.append(marker.size(), ' ');
            out += line;
            out.push_back('\n');
            first = false;
        }
    }
    return out;
}

/// A block quote: every line prefixed with `> ` (a blank line with `>`).
[[nodiscard]] constexpr std::string quote(std::string_view text) {
    std::string out;
    for (const std::string_view line : lines_of(trim_whitespace(text))) {   // LF, CRLF, lone CR alike
        if (trim_space_tab(line).empty()) out += ">";
        else (out += "> ") += line;
        out.push_back('\n');
    }
    return out;
}

/// Front matter around an already serialised body: `---` for YAML, `+++` for TOML.
///
/// Every line ends with `eol` (the document's own line ending). A body line
/// that reads as the closing fence — `---` or `...` for YAML, `+++` for TOML,
/// at column 0 with only spaces or tabs after — would end the front matter
/// early and turn the rest into body text, so it is refused
/// (std::invalid_argument naming the line).
[[nodiscard]] constexpr std::string front_matter(std::string_view body, char marker, std::string_view eol = "\n") {
    const std::string fence(3, marker == '+' ? '+' : '-');
    std::string out = fence;
    out += eol;
    std::size_t number = 0;
    for (std::size_t pos = 0; pos < body.size();) {
        std::size_t end = body.find('\n', pos);
        if (end == std::string_view::npos) end = body.size();
        std::string_view line = body.substr(pos, end - pos);
        if (line.ends_with('\r')) line.remove_suffix(1);
        ++number;
        std::string_view bare = line;
        while (!bare.empty() && (bare.back() == ' ' || bare.back() == '\t')) bare.remove_suffix(1);
        if (bare == fence || (marker != '+' && bare == "...")) {
            throw std::invalid_argument("front matter: line " + decimal(number) + " of the written " +
                                        (marker == '+' ? "TOML" : "YAML") + " is '" + std::string(bare) +
                                        "', which would end the front matter early");
        }
        out += line;
        out += eol;
        pos = end + 1;
    }
    out += fence;
    out += eol;
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
