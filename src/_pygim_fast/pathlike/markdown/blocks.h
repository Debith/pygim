#pragma once
// pathlike/markdown/blocks.h — the block parser: source text -> tree (tree.h).
//
// Pybind-free and constexpr. This is the two-phase algorithm the CommonMark
// spec describes in its appendix ("A parsing strategy") and its reference
// implementation, commonmark.js, follows: each line first walks down the open
// blocks and lets each one claim its prefix (`>`, list indentation, a fence),
// then tries the block starts on what is left, then adds the rest as text to
// the deepest open leaf. What this port adds is the bookkeeping that keeps
// every block's physical lines and every leaf's content segments, so nothing
// is copied and an edit can splice the source exactly.
//
//     basic_block_parser<gfm>{}.parse("# T\n\n> a\n> b\n")
//       -> document [heading(1) "T", quote [paragraph "a" "b"]]
//
// The dialect is a policy (#81): `commonmark` is the spec; `gfm` adds tables
// and task list items here (strikethrough lives in inlines.h). Front matter
// (`---` YAML / `+++` TOML closed by the same line, or `...` for YAML, at the
// very start) is recognised when asked, in either dialect.
//
// Cost: one pass over the lines; per line, a walk down the open blocks
// (their depth) and the block starts on the remainder. Line ends are found
// with std::string_view::find (memchr). The tree holds about 130 bytes per
// block plus 16 per content line.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "syntax.h"
#include "tree.h"

namespace pygim::pathlike::markdown {

// ── dialects ────────────────────────────────────────────────────────────────

/// CommonMark 0.31.2, nothing else.
struct commonmark {
    static constexpr std::string_view name = "commonmark";
    static constexpr bool tables = false, strikethrough = false, tasklists = false;
};
/// GitHub Flavored Markdown: CommonMark plus tables, strikethrough and task list items.
struct gfm {
    static constexpr std::string_view name = "gfm";
    static constexpr bool tables = true, strikethrough = true, tasklists = true;
};

template <class D>
concept Dialect = requires {
    { D::name } -> std::convertible_to<std::string_view>;
    { D::tables } -> std::convertible_to<bool>;
    { D::strikethrough } -> std::convertible_to<bool>;
    { D::tasklists } -> std::convertible_to<bool>;
};

// ── line classifiers ────────────────────────────────────────────────────────
// Each takes a line from its first non-space character (no line ending).
// Proven in tests/static/pathlike_markdown_proofs.cpp.

/// The level of an ATX heading marker (1-6 '#' then a space, tab or the end), else 0.
///     atx_level("## x") -> 2     atx_level("#x") -> 0     atx_level("#######") -> 0
[[nodiscard]] constexpr int atx_level(std::string_view s) noexcept {
    std::size_t n = 0;
    while (n < s.size() && s[n] == '#') ++n;
    if (n == 0 || n > 6) return 0;
    return n == s.size() || is_space_or_tab(s[n]) ? static_cast<int>(n) : 0;
}

/// The text of an ATX heading after its marker: an optional closing run of
/// '#' (preceded by a space or tab, or alone) removed, then trimmed.
///     atx_content(" Title ##  ") -> "Title"     atx_content(" C#") -> "C#"
[[nodiscard]] constexpr std::string_view atx_content(std::string_view s) noexcept {
    s = trim_space_tab(s);
    std::size_t e = s.size();
    while (e > 0 && s[e - 1] == '#') --e;
    if (e == 0) return s.substr(0, 0);   // keeps the position: the caller locates the content by pointer
    if (e < s.size() && is_space_or_tab(s[e - 1])) s = trim_space_tab(s.substr(0, e));
    return s;
}

/// A setext underline: 1 for '='+, 2 for '-'+, followed only by spaces and tabs; else 0.
[[nodiscard]] constexpr int setext_level(std::string_view s) noexcept {
    if (s.empty() || (s[0] != '=' && s[0] != '-')) return 0;
    std::size_t i = 0;
    while (i < s.size() && s[i] == s[0]) ++i;
    while (i < s.size() && is_space_or_tab(s[i])) ++i;
    return i == s.size() ? (s[0] == '=' ? 1 : 2) : 0;
}

/// Where the line starting at `pos` ends, before its line ending, and where
/// the next line starts. A line ends at LF, CRLF or a lone CR (CommonMark).
[[nodiscard]] constexpr std::pair<std::size_t, std::size_t> line_from(std::string_view src, std::size_t pos) noexcept {
    std::size_t e = src.find('\n', pos);   // two memchr-speed searches, not a byte loop: this runs per line
    if (e == std::string_view::npos) e = src.size();
    const std::size_t cr = src.substr(pos, e - pos).find('\r');
    if (cr == std::string_view::npos) return {e, e < src.size() ? e + 1 : e};
    e = pos + cr;   // a CR, alone or before the LF, ends the line
    return {e, e + 1 < src.size() && src[e + 1] == '\n' ? e + 2 : e + 1};
}

/// Where checking `s` for a thematic break (three or more of one of `*`, `-`,
/// `_`, with spaces and tabs between and after) is decided: s.size() + 1 when
/// it is one, otherwise the offset of the byte that rules it out (s.size() when
/// the marks run out first). Every byte before that offset is the first byte's
/// mark or a space, so a check that starts at any of them fails there too —
/// which lets a line of nested list markers be checked once, not once per
/// marker (cmark's thematic_break_kill_pos).
[[nodiscard]] constexpr std::size_t thematic_break_decided(std::string_view s) noexcept {
    if (s.empty() || (s[0] != '*' && s[0] != '-' && s[0] != '_')) return 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == s[0]) ++count;
        else if (!is_space_or_tab(s[i])) return i;
    }
    return count >= 3 ? s.size() + 1 : s.size();
}
[[nodiscard]] constexpr bool is_thematic_break(std::string_view s) noexcept {
    return thematic_break_decided(s) == s.size() + 1;
}

/// An opening code fence: three or more backticks (with no backtick in the
/// info string) or tildes.
struct fence {
    char ch = 0;
    std::size_t len = 0;
};
[[nodiscard]] constexpr fence fence_open(std::string_view s) noexcept {
    if (s.empty() || (s[0] != '`' && s[0] != '~')) return {};
    std::size_t n = 0;
    while (n < s.size() && s[n] == s[0]) ++n;
    if (n < 3) return {};
    if (s[0] == '`' && s.substr(n).find('`') != std::string_view::npos) return {};
    return {s[0], n};
}

/// A closing fence for a block opened with `len` of `ch`: at least as many, then only spaces and tabs.
[[nodiscard]] constexpr bool fence_closes(std::string_view s, char ch, std::size_t len) noexcept {
    std::size_t n = 0;
    while (n < s.size() && s[n] == ch) ++n;
    if (n < 3 || n < len) return false;
    for (std::size_t i = n; i < s.size(); ++i) {
        if (!is_space_or_tab(s[i])) return false;
    }
    return true;
}

namespace detail {
[[nodiscard]] constexpr bool iequals_prefix(std::string_view s, std::string_view lower_prefix) noexcept {
    if (s.size() < lower_prefix.size()) return false;
    for (std::size_t i = 0; i < lower_prefix.size(); ++i) {
        if (ascii_lower(s[i]) != lower_prefix[i]) return false;
    }
    return true;
}
[[nodiscard]] constexpr bool icontains(std::string_view s, std::string_view lower_needle) noexcept {
    if (lower_needle.size() > s.size()) return false;
    for (std::size_t i = 0; i + lower_needle.size() <= s.size(); ++i) {
        if (iequals_prefix(s.substr(i), lower_needle)) return true;
    }
    return false;
}
inline constexpr std::string_view block_tags[] = {
    "address", "article", "aside", "base", "basefont", "blockquote", "body", "caption", "center", "col",
    "colgroup", "dd", "details", "dialog", "dir", "div", "dl", "dt", "fieldset", "figcaption", "figure",
    "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr",
    "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem", "nav", "noframes", "ol",
    "optgroup", "option", "p", "param", "search", "section", "summary", "table", "tbody", "td", "tfoot",
    "th", "thead", "title", "tr", "track", "ul",
};
}  // namespace detail

/// The HTML block start condition (1-7) a line meets, else 0. Condition 7
/// (any complete tag alone on its line) cannot interrupt a paragraph; the
/// parser checks that.
///     html_block_start("<div class=x>") -> 6    html_block_start("<!-- c") -> 2    html_block_start("<a>") -> 7
[[nodiscard]] constexpr int html_block_start(std::string_view s) noexcept {
    if (s.empty() || s[0] != '<') return 0;
    for (std::string_view tag : {"script", "pre", "textarea", "style"}) {
        if (detail::iequals_prefix(s.substr(1), tag)) {
            const std::size_t e = 1 + tag.size();
            if (e == s.size() || is_space_or_tab(s[e]) || s[e] == '>') return 1;
        }
    }
    if (s.starts_with("<!--")) return 2;
    if (s.starts_with("<?")) return 3;
    if (s.size() > 2 && s[1] == '!' && is_ascii_alpha(s[2])) return 4;
    if (s.starts_with("<![CDATA[")) return 5;
    const std::size_t from = s.size() > 1 && s[1] == '/' ? 2 : 1;
    const std::size_t name_end = tag_name_end(s, from);
    if (name_end > from) {
        std::string name;
        for (char c : s.substr(from, name_end - from)) name.push_back(ascii_lower(c));
        for (std::string_view tag : detail::block_tags) {
            if (name == tag) {
                const std::size_t e = name_end;
                if (e == s.size() || is_space_or_tab(s[e]) || s[e] == '>' || s.substr(e).starts_with("/>")) return 6;
                break;
            }
        }
    }
    const std::size_t n = html_open_or_close_tag(s, 0);
    if (n > 0) {
        std::size_t i = n;
        while (i < s.size() && is_space_or_tab(s[i])) ++i;
        if (i == s.size()) return 7;
    }
    return 0;
}

/// Whether a line ends an HTML block of start condition 1-5.
[[nodiscard]] constexpr bool html_block_ends(int type, std::string_view s) noexcept {
    switch (type) {
        case 1: return detail::icontains(s, "</script>") || detail::icontains(s, "</pre>") ||
                       detail::icontains(s, "</textarea>") || detail::icontains(s, "</style>");
        case 2: return s.find("-->") != std::string_view::npos;
        case 3: return s.find("?>") != std::string_view::npos;
        case 4: return s.find('>') != std::string_view::npos;
        case 5: return s.find("]]>") != std::string_view::npos;
        default: return false;
    }
}

/// A GFM table row's cells as [begin, end) offsets into `s`, trimmed; `\|`
/// does not split a cell. A leading and a trailing pipe are optional.
///     table_cells("| a | b\\|c |") -> "a", "b\\|c"
constexpr void table_cells(std::string_view s, std::vector<std::pair<std::size_t, std::size_t>>& out) {
    out.clear();
    std::size_t n = s.size();
    while (n > 0 && is_space_or_tab(s[n - 1])) --n;
    std::size_t i = 0;
    while (i < n && is_space_or_tab(s[i])) ++i;
    if (i < n && s[i] == '|') ++i;
    const auto push = [&](std::size_t b, std::size_t e) {
        while (b < e && is_space_or_tab(s[b])) ++b;
        while (e > b && is_space_or_tab(s[e - 1])) --e;
        out.emplace_back(b, e);
    };
    std::size_t cell = i;
    for (std::size_t j = i; j < n; ++j) {
        if (s[j] == '\\' && j + 1 < n) {
            ++j;
        } else if (s[j] == '|') {
            push(cell, j);
            cell = j + 1;
        }
    }
    std::size_t rest = cell;
    while (rest < n && is_space_or_tab(s[rest])) ++rest;
    if (rest < n || out.empty()) push(cell, n);
}

/// A GFM delimiter row (`| :-- | :-: | --: |`): its column alignments, or
/// false when the line is not one.
constexpr bool table_delimiter_row(std::string_view s, std::vector<align>& out) {
    out.clear();
    std::size_t n = s.size();
    while (n > 0 && is_space_or_tab(s[n - 1])) --n;
    std::size_t i = 0;
    if (i < n && s[i] == '|') ++i;
    while (i < n) {
        while (i < n && is_space_or_tab(s[i])) ++i;
        if (i == n) break;
        const bool left = s[i] == ':';
        if (left) ++i;
        std::size_t dashes = 0;
        while (i < n && s[i] == '-') {
            ++i;
            ++dashes;
        }
        if (dashes == 0) return false;
        const bool right = i < n && s[i] == ':';
        if (right) ++i;
        while (i < n && is_space_or_tab(s[i])) ++i;
        out.push_back(left && right ? align::center : left ? align::left : right ? align::right : align::none);
        if (i == n) break;
        if (s[i] != '|') return false;
        ++i;
    }
    return !out.empty();
}

// ── the parser ──────────────────────────────────────────────────────────────

template <Dialect D = gfm>
class basic_block_parser {
public:
    /// Parses `src` (UTF-8; a leading byte-order mark is skipped and belongs
    /// to no block). `front_matter` recognises a `---`/`+++` block at the start.
    [[nodiscard]] constexpr tree parse(std::string_view src, bool front_matter = true) {
        if (src.size() > 0xFFFFFFFFu) {   // the tree holds 32-bit offsets
            throw std::length_error("markdown: a document of 4 GiB or more is not supported (" + decimal(src.size()) + " bytes)");
        }
        m_src = src;
        m_t = tree{};
        m_t.size = static_cast<std::uint32_t>(src.size());
        block doc;
        doc.type = kind::document;
        doc.first_line = 1;
        m_t.blocks.push_back(doc);
        m_tip = 0;
        std::size_t pos = src.starts_with("\xEF\xBB\xBF") ? 3 : 0;
        if (front_matter) pos = read_front_matter(pos);
        while (pos < src.size()) {
            const auto [e, next] = line_from(src, pos);
            m_ls = static_cast<std::uint32_t>(pos);
            m_next = static_cast<std::uint32_t>(next);
            m_line = src.substr(pos, e - pos);
            m_t.line_starts.push_back(m_ls);
            incorporate_line();
            pos = next;
        }
        m_ls = m_next = static_cast<std::uint32_t>(src.size());
        const std::uint32_t lines = m_line_no;
        while (m_tip != none) finalize(m_tip, lines);
        block& d = m_t.blocks[0];
        d.begin = 0;
        d.end = m_t.size;
        d.first_line = lines ? 1 : 0;
        d.last_line = lines;
        index_definitions();
        return std::move(m_t);
    }

private:
    // ── state of the line being incorporated (names follow commonmark.js) ──
    std::string_view m_src, m_line;
    std::uint32_t m_ls = 0, m_next = 0, m_line_no = 0;
    std::size_t m_offset = 0, m_column = 0, m_nn = 0, m_nn_column = 0, m_indent = 0;
    bool m_indented = false, m_blank = false, m_partial = false, m_all_closed = true;
    bool m_table_opened = false;   // this line was a table's delimiter row: it is not a body row
    std::size_t m_no_break_before = 0;   // no thematic break starts on this line before this offset
    std::uint32_t m_tip = 0, m_oldtip = 0, m_matched = 0;
    tree m_t;
    std::vector<std::pair<std::size_t, std::size_t>> m_cells;
    std::vector<align> m_aligns;

    [[nodiscard]] constexpr block& at(std::uint32_t i) { return m_t.blocks[i]; }
    [[nodiscard]] constexpr int peek(std::size_t i) const noexcept {
        return i < m_line.size() ? static_cast<unsigned char>(m_line[i]) : -1;
    }
    [[nodiscard]] constexpr std::string_view text(const segment& s) const noexcept {
        return m_src.substr(s.begin, s.end - s.begin);
    }
    [[nodiscard]] constexpr std::uint32_t line_end(std::uint32_t n) const noexcept {
        return n < m_t.line_starts.size() ? m_t.line_starts[n] : m_next;
    }
    [[nodiscard]] static constexpr bool can_contain(kind parent, kind child) noexcept {
        switch (parent) {
            case kind::document:
            case kind::quote:
            case kind::item: return child != kind::item;
            case kind::list: return child == kind::item;
            default: return false;
        }
    }
    [[nodiscard]] static constexpr bool accepts_lines(kind k) noexcept {
        return k == kind::paragraph || k == kind::code || k == kind::html || k == kind::table;
    }

    // ── column bookkeeping: tabs count to the next multiple of 4 ──
    constexpr void find_next_nonspace() noexcept {
        std::size_t i = m_offset, cols = m_column;
        while (i < m_line.size()) {
            if (m_line[i] == ' ') {
                ++i;
                ++cols;
            } else if (m_line[i] == '\t') {
                ++i;
                cols += 4 - cols % 4;
            } else {
                break;
            }
        }
        m_blank = i >= m_line.size();
        m_nn = i;
        m_nn_column = cols;
        m_indent = m_nn_column - m_column;
        m_indented = m_indent >= 4;
    }
    constexpr void advance_next_nonspace() noexcept {
        m_offset = m_nn;
        m_column = m_nn_column;
        m_partial = false;
    }
    constexpr void advance_offset(std::size_t count, bool columns) noexcept {
        while (count > 0 && m_offset < m_line.size()) {
            if (m_line[m_offset] == '\t') {
                const std::size_t to_tab = 4 - m_column % 4;
                if (columns) {
                    m_partial = to_tab > count;
                    const std::size_t step = to_tab > count ? count : to_tab;
                    m_column += step;
                    m_offset += m_partial ? 0 : 1;
                    count -= step;
                } else {
                    m_partial = false;
                    m_column += to_tab;
                    m_offset += 1;
                    count -= 1;
                }
            } else {
                m_partial = false;
                m_offset += 1;
                m_column += 1;
                count -= 1;
            }
        }
    }

    // ── the tree ──
    constexpr std::uint32_t add_child(kind type) {
        while (!can_contain(at(m_tip).type, type)) finalize(m_tip, m_line_no - 1);
        const auto idx = static_cast<std::uint32_t>(m_t.blocks.size());
        block b;
        b.type = type;
        b.parent = m_tip;
        b.first_line = m_line_no;
        b.begin = m_ls;
        b.seg = static_cast<std::uint32_t>(m_t.segments.size());
        b.prev = at(m_tip).last;
        if (b.prev != none) at(b.prev).next = idx;
        else at(m_tip).first = idx;
        at(m_tip).last = idx;
        m_t.blocks.push_back(b);
        m_tip = idx;
        return idx;
    }
    constexpr void unlink(std::uint32_t i) {
        block& b = at(i);
        if (b.parent == none) return;
        if (b.prev != none) at(b.prev).next = b.next;
        else at(b.parent).first = b.next;
        if (b.next != none) at(b.next).prev = b.prev;
        else at(b.parent).last = b.prev;
        b.parent = b.prev = b.next = none;
    }
    constexpr void insert_before(std::uint32_t i, std::uint32_t sibling) {
        block& b = at(i);
        b.parent = at(sibling).parent;
        b.next = sibling;
        b.prev = at(sibling).prev;
        if (b.prev != none) at(b.prev).next = i;
        else at(b.parent).first = i;
        at(sibling).prev = i;
    }
    constexpr void add_line() {
        block& b = at(m_tip);
        if (b.nseg == 0) {
            b.seg = static_cast<std::uint32_t>(m_t.segments.size());
            b.first_line = m_line_no;
            b.begin = m_ls;
        }
        std::uint32_t pad = 0;
        if (m_partial) {
            m_offset += 1;
            pad = static_cast<std::uint32_t>(4 - m_column % 4);
        }
        const std::size_t from = m_offset < m_line.size() ? m_offset : m_line.size();
        m_t.segments.push_back({m_ls + static_cast<std::uint32_t>(from), m_ls + static_cast<std::uint32_t>(m_line.size()), m_line_no, pad});
        ++b.nseg;
    }
    constexpr void close_unmatched() {
        if (m_all_closed) return;
        while (m_oldtip != m_matched) {
            const std::uint32_t parent = at(m_oldtip).parent;
            finalize(m_oldtip, m_line_no - 1);
            m_oldtip = parent;
        }
        m_all_closed = true;
    }

    // ── one line ──
    constexpr void incorporate_line() {
        std::uint32_t container = 0;
        m_oldtip = m_tip;
        m_offset = m_column = 0;
        m_blank = m_partial = m_table_opened = false;
        m_no_break_before = 0;
        ++m_line_no;

        for (;;) {   // let each open block claim its prefix
            const std::uint32_t last = at(container).last;
            if (last == none || !at(last).open) break;
            container = last;
            find_next_nonspace();
            const int r = continue_block(container);
            if (r == 2) return;
            if (r == 1) {   // not matched: the open blocks from here down close unless a lazy line keeps them
                container = at(container).parent;
                break;
            }
        }
        m_all_closed = container == m_oldtip;
        m_matched = container;

        bool matched_leaf = at(container).type != kind::paragraph && at(container).type != kind::table &&
                            accepts_lines(at(container).type);
        while (!matched_leaf) {   // new block starts
            find_next_nonspace();
            if (!m_indented && !maybe_special(peek(m_nn))) {
                advance_next_nonspace();
                break;
            }
            const int r = start_block(container);
            if (r == 0) {
                advance_next_nonspace();
                break;
            }
            container = m_tip;
            if (r == 2) matched_leaf = true;
        }

        if (!m_all_closed && !m_blank && at(m_tip).type == kind::paragraph) {
            add_line();   // a lazy paragraph continuation
            return;
        }
        close_unmatched();
        const kind t = at(container).type;
        if (accepts_lines(t)) {
            if (t == kind::table) {
                if (!m_table_opened) add_row(container);
            } else {
                add_line();
            }
            if (t == kind::html && at(container).html_type <= 5 &&
                html_block_ends(static_cast<int>(at(container).html_type), m_line.substr(std::min(m_offset, m_line.size())))) {
                finalize(container, m_line_no);
            }
        } else if (m_offset < m_line.size() && !m_blank) {
            add_child(kind::paragraph);
            advance_next_nonspace();
            add_line();
        }
    }

    [[nodiscard]] static constexpr bool maybe_special(int c) noexcept {
        switch (c) {
            case '#': case '`': case '~': case '*': case '+': case '_': case '=': case '<': case '>': case '-':
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                return true;
            case '|': case ':':
                return D::tables;
            default:
                return false;
        }
    }

    /// 0: matched, keep going; 1: not matched; 2: the line is consumed (a closing fence).
    constexpr int continue_block(std::uint32_t c) {
        block& b = at(c);
        switch (b.type) {
            case kind::quote:
                if (!m_indented && peek(m_nn) == '>') {
                    advance_next_nonspace();
                    advance_offset(1, false);
                    if (peek(m_offset) == ' ' || peek(m_offset) == '\t') advance_offset(1, true);
                    return 0;
                }
                return 1;
            case kind::item:
                if (m_blank) {
                    if (b.first == none) return 1;   // a blank line after an empty item ends it
                    advance_next_nonspace();
                } else if (m_indent >= b.marker_offset + b.padding) {
                    advance_offset(b.marker_offset + b.padding, true);
                } else {
                    return 1;
                }
                return 0;
            case kind::heading:
            case kind::thematic_break:
                return 1;
            case kind::code:
                if (b.fenced) {
                    if (m_indent <= 3 && peek(m_nn) == static_cast<unsigned char>(b.marker) &&
                        fence_closes(m_line.substr(m_nn), b.marker, b.fence_length)) {
                        finalize(c, m_line_no);
                        return 2;
                    }
                    std::size_t i = b.fence_offset;
                    while (i > 0 && (peek(m_offset) == ' ' || peek(m_offset) == '\t')) {
                        advance_offset(1, true);
                        --i;
                    }
                    return 0;
                }
                if (m_indent >= 4) advance_offset(4, true);
                else if (m_blank) advance_next_nonspace();
                else return 1;
                return 0;
            case kind::html:
                return m_blank && (b.html_type == 6 || b.html_type == 7) ? 1 : 0;
            case kind::paragraph:
            case kind::table:
                return m_blank ? 1 : 0;
            default:
                return 0;
        }
    }

    /// 0: nothing starts here; 1: a container started; 2: a leaf started (the rest of the line is its).
    constexpr int start_block(std::uint32_t container) {
        const std::string_view rest = m_line.substr(m_nn);
        const kind ctype = at(container).type;
        if (!m_indented) {
            if (peek(m_nn) == '>') {   // block quote
                advance_next_nonspace();
                advance_offset(1, false);
                if (peek(m_offset) == ' ' || peek(m_offset) == '\t') advance_offset(1, true);
                close_unmatched();
                add_child(kind::quote);
                return 1;
            }
            if (const int level = atx_level(rest)) {   // ATX heading
                advance_next_nonspace();
                std::size_t marker = static_cast<std::size_t>(level);
                while (marker < rest.size() && is_space_or_tab(rest[marker])) ++marker;
                advance_offset(marker, false);
                close_unmatched();
                const std::uint32_t h = add_child(kind::heading);
                at(h).level = static_cast<std::uint32_t>(level);
                const std::string_view tail = m_line.substr(std::min(m_offset, m_line.size()));
                const std::string_view content = atx_content(tail);
                const std::uint32_t b = m_ls + static_cast<std::uint32_t>(content.data() - m_line.data());
                m_t.segments.push_back({b, b + static_cast<std::uint32_t>(content.size()), m_line_no, 0});
                at(h).seg = static_cast<std::uint32_t>(m_t.segments.size() - 1);
                at(h).nseg = 1;
                advance_offset(m_line.size() - m_offset, false);
                return 2;
            }
            if (const fence f = fence_open(rest); f.len) {   // fenced code
                close_unmatched();
                const std::uint32_t c = add_child(kind::code);
                block& b = at(c);
                b.fenced = true;
                b.marker = f.ch;
                b.fence_length = static_cast<std::uint32_t>(f.len);
                b.fence_offset = static_cast<std::uint32_t>(m_indent);
                advance_next_nonspace();
                advance_offset(f.len, false);
                return 2;
            }
            if (peek(m_nn) == '<') {   // HTML block
                const int type = html_block_start(rest);
                const bool can_interrupt = ctype != kind::paragraph &&
                                           !(!m_all_closed && !m_blank && at(m_tip).type == kind::paragraph);
                if (type != 0 && (type < 7 || can_interrupt)) {
                    close_unmatched();
                    const std::uint32_t h = add_child(kind::html);
                    at(h).html_type = static_cast<std::uint32_t>(type);
                    return 2;   // the offset stays: leading spaces belong to the block
                }
            }
            if (ctype == kind::paragraph) {   // setext heading
                if (const int level = setext_level(rest)) {
                    close_unmatched();
                    consume_definitions(container);
                    if (at(container).nseg > 0) {
                        block& h = at(container);
                        h.type = kind::heading;
                        h.level = static_cast<std::uint32_t>(level);
                        h.setext = true;
                        m_tip = container;
                        advance_offset(m_line.size() - m_offset, false);
                        return 2;
                    }
                }
            }
            if (m_nn >= m_no_break_before) {
                const std::size_t decided = thematic_break_decided(rest);
                if (decided == rest.size() + 1) {
                    close_unmatched();
                    add_child(kind::thematic_break);
                    advance_offset(m_line.size() - m_offset, false);
                    return 2;
                }
                m_no_break_before = m_nn + decided;
            }
        }
        if (!m_indented || ctype == kind::list) {   // list item
            block data;
            if (list_marker(container, data)) {
                close_unmatched();
                if (at(m_tip).type != kind::list || !lists_match(at(m_tip), data)) {
                    const std::uint32_t l = add_child(kind::list);
                    copy_list_data(data, at(l));
                }
                const std::uint32_t it = add_child(kind::item);
                copy_list_data(data, at(it));
                at(it).marker_offset = data.marker_offset;
                at(it).padding = data.padding;
                return 1;
            }
        }
        if (m_indented && at(m_tip).type != kind::paragraph && !m_blank) {   // indented code
            advance_offset(4, true);
            close_unmatched();
            add_child(kind::code);
            return 2;
        }
        if constexpr (D::tables) {
            if (!m_indented && ctype == kind::paragraph && open_table(container)) return 2;
        }
        return 0;
    }

    static constexpr void copy_list_data(const block& from, block& to) noexcept {
        to.ordered = from.ordered;
        to.marker = from.marker;
        to.start = from.start;
        to.tight = true;
    }
    [[nodiscard]] static constexpr bool lists_match(const block& list, const block& item) noexcept {
        return list.ordered == item.ordered && list.marker == item.marker;
    }

    /// A list marker at the next non-space position; fills `data` (ordered,
    /// marker, start, marker_offset, padding) and advances past it.
    constexpr bool list_marker(std::uint32_t container, block& data) {
        if (m_indent >= 4) return false;
        const std::string_view rest = m_line.substr(m_nn);
        if (rest.empty()) return false;
        const bool interrupts = at(container).type == kind::paragraph;
        std::size_t len = 0;
        if (rest[0] == '*' || rest[0] == '+' || rest[0] == '-') {
            data.ordered = false;
            data.marker = rest[0];
            len = 1;
        } else {
            std::size_t d = 0;
            std::uint32_t n = 0;
            while (d < rest.size() && d < 9 && is_ascii_digit(rest[d])) n = n * 10 + static_cast<std::uint32_t>(rest[d++] - '0');
            if (d == 0 || d >= rest.size() || (rest[d] != '.' && rest[d] != ')')) return false;
            if (interrupts && n != 1) return false;
            data.ordered = true;
            data.start = n;
            data.marker = rest[d];
            len = d + 1;
        }
        const int after = peek(m_nn + len);
        if (!(after == -1 || after == ' ' || after == '\t')) return false;
        if (interrupts) {   // an item interrupting a paragraph must not start blank
            bool content = false;
            for (std::size_t i = m_nn + len; i < m_line.size(); ++i) {
                if (!is_space_or_tab(m_line[i])) {
                    content = true;
                    break;
                }
            }
            if (!content) return false;
        }
        data.marker_offset = static_cast<std::uint32_t>(m_indent);
        advance_next_nonspace();
        advance_offset(len, true);
        const std::size_t spaces_col = m_column, spaces_off = m_offset;
        int next = -1;
        do {
            advance_offset(1, true);
            next = peek(m_offset);
        } while (m_column - spaces_col < 5 && (next == ' ' || next == '\t'));
        const bool blank_item = peek(m_offset) == -1;
        const std::size_t spaces = m_column - spaces_col;
        if (spaces >= 5 || spaces < 1 || blank_item) {
            data.padding = static_cast<std::uint32_t>(len + 1);
            m_column = spaces_col;
            m_offset = spaces_off;
            if (peek(m_offset) == ' ' || peek(m_offset) == '\t') advance_offset(1, true);
        } else {
            data.padding = static_cast<std::uint32_t>(len + spaces);
        }
        return true;
    }

    /// GFM: the current line is a delimiter row and the paragraph's last line
    /// a header row with as many cells -> the paragraph's last line becomes a table.
    /// A paragraph whose lines were all link definitions has no last line
    /// (`[x]: /u` then `-`: the setext check consumed them and left it open).
    constexpr bool open_table(std::uint32_t p) {
        if (at(p).nseg == 0) return false;
        if (!table_delimiter_row(m_line.substr(m_nn), m_aligns)) return false;
        const segment head = m_t.segments[at(p).seg + at(p).nseg - 1];
        table_cells(text(head), m_cells);
        if (m_cells.size() != m_aligns.size()) return false;
        close_unmatched();
        const std::uint32_t parent = at(p).parent;
        at(p).nseg -= 1;
        if (at(p).nseg == 0) {
            unlink(p);
            m_tip = parent;
        } else {
            finalize(p, head.line - 1);
        }
        const std::uint32_t t = add_child(kind::table);
        block& b = at(t);
        b.first_line = head.line;
        b.begin = m_t.line_start(head.line);
        b.columns = static_cast<std::uint32_t>(m_aligns.size());
        b.align = static_cast<std::uint32_t>(m_t.aligns.size());
        m_t.aligns.insert(m_t.aligns.end(), m_aligns.begin(), m_aligns.end());
        b.seg = static_cast<std::uint32_t>(m_t.segments.size());
        for (const auto& [cb, ce] : m_cells) {
            m_t.segments.push_back({head.begin + static_cast<std::uint32_t>(cb), head.begin + static_cast<std::uint32_t>(ce), head.line, 0});
        }
        b.nseg = b.columns;
        b.rows = 1;
        advance_offset(m_line.size() - m_offset, false);
        m_table_opened = true;
        return true;
    }

    /// A table body row: its cells, padded with empty ones or cut to the header's count.
    constexpr void add_row(std::uint32_t t) {
        const std::size_t from = std::min(m_offset, m_line.size());
        table_cells(m_line.substr(from), m_cells);
        const std::uint32_t base = m_ls + static_cast<std::uint32_t>(from);
        block& b = at(t);
        for (std::uint32_t c = 0; c < b.columns; ++c) {
            if (c < m_cells.size()) {
                m_t.segments.push_back({base + static_cast<std::uint32_t>(m_cells[c].first), base + static_cast<std::uint32_t>(m_cells[c].second), m_line_no, 0});
            } else {
                const std::uint32_t e = m_ls + static_cast<std::uint32_t>(m_line.size());
                m_t.segments.push_back({e, e, m_line_no, 0});
            }
        }
        b.nseg += b.columns;
        b.rows += 1;
    }

    // ── closing a block ──
    constexpr void finalize(std::uint32_t i, std::uint32_t line) {
        const std::uint32_t above = at(i).parent;
        {
            block& b = at(i);
            b.open = false;
            b.last_line = std::max(line, b.first_line);
            b.end = line_end(b.last_line);
        }
        switch (at(i).type) {
            case kind::paragraph:
                consume_definitions(i);
                if (at(i).nseg == 0) unlink(i);   // only definitions (or nothing) were in it
                break;
            case kind::code: {
                block& b = at(i);
                if (b.fenced) {
                    if (b.nseg > 0) {
                        b.info = m_t.segments[b.seg];
                        ++b.seg;
                        --b.nseg;
                    }
                } else {
                    while (b.nseg > 0 && spaces_only(m_t.segments[b.seg + b.nseg - 1])) --b.nseg;
                }
                break;
            }
            case kind::html: {
                block& b = at(i);
                while (b.nseg > 0 && spaces_only(m_t.segments[b.seg + b.nseg - 1])) --b.nseg;
                break;
            }
            case kind::list:
                finalize_list(i);
                break;
            case kind::item:
                finalize_item(i);
                break;
            default:
                break;
        }
        m_tip = above;
    }

    [[nodiscard]] constexpr bool spaces_only(const segment& s) const noexcept {
        for (char c : text(s)) {
            if (c != ' ') return false;
        }
        return true;
    }

    constexpr void finalize_list(std::uint32_t l) {
        // A list is loose when an item, or two blocks inside an item, are separated by a blank line.
        const auto gap_after = [&](std::uint32_t b) {
            return at(b).next != none && at(b).last_line + 1 != at(at(b).next).first_line;
        };
        bool tight = true;
        for (std::uint32_t it = at(l).first; it != none && tight; it = at(it).next) {
            if (gap_after(it)) tight = false;
            for (std::uint32_t sub = at(it).first; sub != none && tight; sub = at(sub).next) {
                if (gap_after(sub)) tight = false;
            }
        }
        block& b = at(l);
        b.tight = tight;
        for (std::uint32_t it = b.first; it != none; it = at(it).next) at(it).tight = tight;
        if (b.last != none) {
            b.last_line = at(b.last).last_line;
            b.end = at(b.last).end;
        }
    }

    constexpr void finalize_item(std::uint32_t it) {
        block& b = at(it);
        if (b.last != none) {
            b.last_line = at(b.last).last_line;
            b.end = at(b.last).end;
        } else {
            b.last_line = b.first_line;
            b.end = line_end(b.first_line);
        }
        if constexpr (D::tasklists) {
            const std::uint32_t p = b.first;
            if (p == none || at(p).type != kind::paragraph || at(p).nseg == 0) return;
            segment& s = m_t.segments[at(p).seg];
            const std::string_view t = text(s);
            if (t.size() < 4 || t[0] != '[' || t[2] != ']' || !is_space_or_tab(t[3])) return;
            if (t[1] != ' ' && t[1] != 'x' && t[1] != 'X') return;
            std::size_t k = 3;
            while (k < t.size() && is_space_or_tab(t[k])) ++k;
            if (k == t.size() && at(p).nseg == 1) return;   // a marker with nothing after it is text
            at(it).task = t[1] == ' ' ? 0 : 1;
            s.begin += static_cast<std::uint32_t>(k);
        }
    }

    /// Link reference definitions at the start of paragraph `p` become
    /// `definition` blocks before it; a paragraph left empty is removed
    /// unless it is still open (a setext underline may follow).
    constexpr void consume_definitions(std::uint32_t p) {
        if (at(p).nseg == 0 || m_src[m_t.segments[at(p).seg].begin] != '[') return;
        std::string content;
        std::vector<std::size_t> starts;   // where each segment starts in `content`
        for (std::uint32_t k = 0; k < at(p).nseg; ++k) {
            if (k) content.push_back('\n');
            starts.push_back(content.size());
            content += text(m_t.segments[at(p).seg + k]);
        }
        std::size_t pos = 0;
        std::uint32_t used = 0;
        while (pos < content.size() && content[pos] == '[') {
            auto ref = link_reference(std::string_view(content).substr(pos));
            if (!ref) break;
            const std::size_t end = pos + ref->len;
            std::uint32_t k = used;
            while (k < at(p).nseg && starts[k] < end) ++k;
            const std::uint32_t d = add_definition(p, used, k - used, std::move(*ref));
            (void)d;
            used = k;
            pos = end;
        }
        if (used == 0) return;
        block& b = at(p);
        b.seg += used;
        b.nseg -= used;
        if (b.nseg > 0) {
            b.first_line = m_t.segments[b.seg].line;
            b.begin = m_t.line_start(b.first_line);
        }
    }

    constexpr std::uint32_t add_definition(std::uint32_t p, std::uint32_t from, std::uint32_t count, reference&& ref) {
        const auto idx = static_cast<std::uint32_t>(m_t.blocks.size());
        block d;
        d.type = kind::definition;
        d.open = false;
        d.seg = at(p).seg + from;
        d.nseg = count;
        d.first_line = m_t.segments[d.seg].line;
        d.last_line = m_t.segments[d.seg + count - 1].line;
        d.begin = m_t.line_start(d.first_line);
        d.end = line_end(d.last_line);
        d.def = static_cast<std::uint32_t>(m_t.definitions.size());
        m_t.blocks.push_back(d);
        insert_before(idx, p);
        m_t.definitions.push_back({std::move(ref.label), std::move(ref.destination), std::move(ref.title), idx});
        return idx;
    }

    constexpr void index_definitions() {
        // by label, then document order (std::sort, not stable_sort: only the
        // former is constexpr before C++26) — so the first definition of a label
        // registers first, and every insert lands at the end of the flat engine
        std::vector<std::uint32_t> order(m_t.definitions.size());
        for (std::uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
            const std::string& la = m_t.definitions[a].label;
            const std::string& lb = m_t.definitions[b].label;
            return la < lb || (la == lb && a < b);
        });
        for (std::uint32_t i : order) m_t.labels.register_value(m_t.definitions[i].label, i);   // a later duplicate is kept out
    }

    /// `---` (YAML) or `+++` (TOML) on the first line, closed by the same
    /// line (or `...` for YAML): a front_matter block of the lines between.
    /// A fence starts at column 0 — spaces or tabs may follow it, never
    /// precede it — so an indented `---` inside a YAML block scalar is the
    /// scalar's text. Lines end as the body's do: LF, CRLF or a lone CR.
    /// Returns where block parsing starts.
    constexpr std::size_t read_front_matter(std::size_t pos) {
        struct line {
            std::size_t begin, end, next;   // end before the line ending
        };
        const auto line_at = [&](std::size_t p) {
            const auto [e, next] = line_from(m_src, p);
            return line{p, e, next};
        };
        const auto fence = [&](const line& l) {   // the line without trailing spaces and tabs
            std::string_view v = m_src.substr(l.begin, l.end - l.begin);
            while (!v.empty() && is_space_or_tab(v.back())) v.remove_suffix(1);
            return v;
        };
        const line first = line_at(pos);
        const std::string_view open = fence(first);
        if (open != "---" && open != "+++") return pos;
        std::vector<line> lines{first};
        for (std::size_t p = first.next; p < m_src.size();) {
            lines.push_back(line_at(p));
            const std::string_view l = fence(lines.back());
            if (l == open || (open[0] == '-' && l == "...")) {
                block fm;
                fm.type = kind::front_matter;
                fm.open = false;
                fm.parent = 0;
                fm.marker = open[0];
                fm.first_line = 1;
                fm.last_line = static_cast<std::uint32_t>(lines.size());
                fm.begin = static_cast<std::uint32_t>(pos);
                fm.end = static_cast<std::uint32_t>(lines.back().next);
                fm.seg = static_cast<std::uint32_t>(m_t.segments.size());
                for (std::size_t k = 0; k < lines.size(); ++k) {
                    m_t.line_starts.push_back(static_cast<std::uint32_t>(lines[k].begin));
                    if (k == 0 || k + 1 == lines.size()) continue;
                    m_t.segments.push_back({static_cast<std::uint32_t>(lines[k].begin), static_cast<std::uint32_t>(lines[k].end),
                                            static_cast<std::uint32_t>(k + 1), 0});
                    ++fm.nseg;
                }
                m_t.blocks.push_back(fm);
                m_t.blocks[0].first = m_t.blocks[0].last = 1;
                m_line_no = static_cast<std::uint32_t>(lines.size());
                return lines.back().next;
            }
            p = lines.back().next;
        }
        return pos;
    }
};

using block_parser = basic_block_parser<gfm>;

}  // namespace pygim::pathlike::markdown
