#pragma once
// pathlike/markdown/document.h — a parsed markdown document: source, tree, sections, edits.
//
// Pybind-free and constexpr. A document OWNS its source text and the tree
// parsed from it (blocks.h); everything else is derived on demand — a
// block's inline text, its HTML, the heading slugs, the sections — so
// reading a 2 MB file costs one block parse, and a block nobody asks about
// is never inline-parsed. A document never changes: an edit returns the new
// source (splice), and the caller parses that into a new document.
//
// Worked example:
//
//     1  ---
//     2  title: Notes
//     3  ---
//     4  # Notes
//     5
//     6  ## Usage
//     7  Run `oo`.
//     8
//     9  ## Usage
//
//     front_matter()    -> block #1 (lines 1-3), body "title: Notes"
//     headings()        -> #2 "Notes" notes, #3 "Usage" usage, #5 "Usage" usage-1   (github_slug)
//     sections()        -> "Notes" lines 4-9 (holds both), "Usage" lines 6-8, "Usage" line 9
//     splice(sections()[1], "## Use\n") -> lines 6-8 replaced by "## Use\n\n" — the
//                          blank line that separated it from line 9 is kept, nothing else moves
//
// Lifetime and threading: immutable after construction, apart from the
// heading and section caches, which fill on first use; a caller sharing one
// document across threads fills them first (or holds a lock, as the Python
// adapter does through the GIL).

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blocks.h"
#include "inlines.h"
#include "render.h"
#include "scan.h"
#include "slug.h"
#include "tree.h"

namespace pygim::pathlike::markdown {

/// A heading with its text and its anchor.
struct heading_info {
    std::uint32_t block = none;
    std::string title;   // the heading's text content, line breaks as spaces
    std::string slug;    // unique within the document
};

/// A top-level heading and everything up to the next top-level heading of
/// the same or a higher level. Its byte span ends where that heading begins,
/// so it owns the blank lines before it.
struct section {
    std::uint32_t heading = none;   // the heading block
    std::uint32_t level = 0;
    std::uint32_t begin = 0, end = 0;
    std::uint32_t first_line = 0, last_line = 0;
    std::uint32_t parent = none;    // index into sections(), or none at the top
    std::uint32_t slug_index = 0;   // index into headings()
};

template <Dialect D = gfm, SlugPolicy Slug = github_slug, class Scan = default_scan>
class basic_document {
public:
    using dialect = D;
    using slug_policy = Slug;

    /// Parses `source`. `front_matter` recognises `---`/`+++` front matter at the start.
    constexpr explicit basic_document(std::string source, bool front_matter = true)
        : m_source(std::move(source)), m_front_matter(front_matter),
          m_tree(basic_block_parser<D>{}.parse(m_source, front_matter)) {}

    [[nodiscard]] constexpr const std::string& source() const noexcept { return m_source; }
    [[nodiscard]] constexpr const tree& structure() const noexcept { return m_tree; }
    [[nodiscard]] constexpr const block& at(std::uint32_t i) const { return m_tree.blocks[i]; }
    [[nodiscard]] constexpr bool recognises_front_matter() const noexcept { return m_front_matter; }

    /// The source bytes of block `i`: its whole lines, markers included.
    [[nodiscard]] constexpr std::string_view text(std::uint32_t i) const {
        const block& b = at(i);
        return std::string_view(m_source).substr(b.begin, b.end - b.begin);
    }

    /// The inline markdown of a paragraph or heading ("Run `oo`."), or a table
    /// cell's (row 0 is the header), without the block's markers.
    [[nodiscard]] constexpr std::string inline_source(std::uint32_t i) const { return leaf_text(m_source, m_tree, at(i)); }
    [[nodiscard]] constexpr std::string cell_source(std::uint32_t table, std::uint32_t row, std::uint32_t col) const {
        const block& b = at(table);
        return std::string(trim_whitespace(cell_text(m_source, m_tree.segments[b.seg + row * b.columns + col])));
    }

    /// The text a reader sees: inline markup resolved ("Run oo."). A code
    /// block gives its code, a table its cells (tab between, line per row),
    /// a container its blocks one blank line apart; HTML, front matter,
    /// definitions and breaks give nothing.
    [[nodiscard]] constexpr std::string plain(std::uint32_t i) const {
        std::string out;
        append_plain_block(i, out);
        return out;
    }
    [[nodiscard]] constexpr std::string cell_plain(std::uint32_t table, std::uint32_t row, std::uint32_t col) const {
        return inline_plain(cell_source(table, row, col));
    }

    /// The code of a code block, every line ended by '\n'; its info string, unescaped.
    [[nodiscard]] constexpr std::string code(std::uint32_t i) const { return literal_text(m_source, m_tree, at(i), true); }
    [[nodiscard]] constexpr std::string info(std::uint32_t i) const { return info_text(m_source, at(i)); }
    /// The raw text of an HTML block; the body of the front matter.
    [[nodiscard]] constexpr std::string raw(std::uint32_t i) const { return literal_text(m_source, m_tree, at(i), false); }

    /// HTML for the whole document, or for block `i`, as CommonMark's reference renderer writes it.
    [[nodiscard]] constexpr std::string html(std::uint32_t i = 0) const {
        return basic_html_renderer<D, Scan>{}.render(m_source, m_tree, i);
    }

    /// The front matter block, or none.
    [[nodiscard]] constexpr std::uint32_t front_matter() const noexcept {
        const std::uint32_t first = at(0).first;
        return first != none && at(first).type == kind::front_matter ? first : none;
    }

    /// Every heading, in document order (those inside quotes and lists too), with unique slugs.
    [[nodiscard]] constexpr const std::vector<heading_info>& headings() const {
        if (!m_headings) {
            std::vector<heading_info> out;
            std::vector<std::string> taken;
            walk([&](std::uint32_t i) {
                if (at(i).type != kind::heading) return;
                heading_info h;
                h.block = i;
                h.title = plain(i);
                for (char& c : h.title) {
                    if (c == '\n') c = ' ';
                }
                h.slug = unique(slug<Slug>(h.title), taken);
                out.push_back(std::move(h));
            });
            m_headings = std::move(out);
        }
        return *m_headings;
    }

    /// The sections of the top-level headings, in document order.
    [[nodiscard]] constexpr const std::vector<section>& sections() const {
        if (!m_sections) {
            const std::vector<heading_info>& hs = headings();
            std::vector<section> out;
            std::vector<std::uint32_t> open;   // indices of sections still open, deepest last
            for (std::uint32_t k = 0; k < hs.size(); ++k) {
                const block& h = at(hs[k].block);
                if (h.parent != 0) continue;   // only top-level headings divide the document
                while (!open.empty() && out[open.back()].level >= h.level) {
                    close(out[open.back()], h.begin);
                    open.pop_back();
                }
                section s;
                s.heading = hs[k].block;
                s.level = h.level;
                s.begin = h.begin;
                s.first_line = h.first_line;
                s.parent = open.empty() ? none : open.back();
                s.slug_index = k;
                open.push_back(static_cast<std::uint32_t>(out.size()));
                out.push_back(s);
            }
            for (std::uint32_t o : open) close(out[o], static_cast<std::uint32_t>(m_source.size()));
            m_sections = std::move(out);
        }
        return *m_sections;
    }

    /// The 1-based line holding byte `offset` (the last line for the end of the source).
    [[nodiscard]] constexpr std::uint32_t line_of(std::uint32_t offset) const noexcept {
        const auto& ls = m_tree.line_starts;
        const auto it = std::upper_bound(ls.begin(), ls.end(), offset);
        return static_cast<std::uint32_t>(it - ls.begin());
    }

    /// The source with bytes [begin, end) replaced by `text`, keeping the
    /// separator that followed the region: `text`'s own trailing line ends
    /// and blank lines are dropped and the region's put back, so a block
    /// stays separated from the next one exactly as before. An empty `text`
    /// deletes the region with its separator.
    [[nodiscard]] constexpr std::string splice(std::uint32_t begin, std::uint32_t end, std::string_view text) const {
        const std::string_view src = m_source;
        const std::string_view region = src.substr(begin, end - begin);
        std::string out(src.substr(0, begin));
        if (!text.empty()) {
            out += text.substr(0, trailing_separator(text));
            const std::size_t cut = trailing_separator(region);
            if (cut < region.size()) out += region.substr(cut);
            else if (end < src.size()) out.push_back('\n');   // the region had no line end: still end the new text's line
        }
        out += src.substr(end);
        return out;
    }

private:
    std::string m_source;
    bool m_front_matter = true;
    tree m_tree;
    mutable std::optional<std::vector<heading_info>> m_headings;
    mutable std::optional<std::vector<section>> m_sections;

    /// Pre-order over every block below the document, without recursion.
    template <class F>
    constexpr void walk(F&& f) const {
        std::uint32_t cur = at(0).first;
        while (cur != none) {
            f(cur);
            if (at(cur).first != none) {
                cur = at(cur).first;
                continue;
            }
            while (cur != none && at(cur).next == none) {
                cur = at(cur).parent;
                if (cur == 0) cur = none;
            }
            if (cur != none) cur = at(cur).next;
        }
    }

    [[nodiscard]] constexpr std::string inline_plain(std::string_view text) const {
        const inline_tree it = basic_inline_parser<D, Scan>{}.parse(text, m_tree);
        std::string out;
        append_plain(it, 0, out);
        return out;
    }

    constexpr void append_plain_block(std::uint32_t i, std::string& out) const {
        const block& b = at(i);
        switch (b.type) {
            case kind::paragraph:
            case kind::heading:
                out += inline_plain(leaf_text(m_source, m_tree, b));
                break;
            case kind::code:
                out += code(i);
                break;
            case kind::table:
                for (std::uint32_t r = 0; r < b.rows; ++r) {
                    for (std::uint32_t c = 0; c < b.columns; ++c) {
                        if (c) out.push_back('\t');
                        out += cell_plain(i, r, c);
                    }
                    out.push_back('\n');
                }
                break;
            case kind::document:
            case kind::quote:
            case kind::list:
            case kind::item: {
                bool first = true;
                for (std::uint32_t c = b.first; c != none; c = at(c).next) {
                    const std::size_t before = out.size();
                    if (!first) out += "\n\n";
                    const std::size_t mark = out.size();
                    append_plain_block(c, out);
                    if (out.size() == mark) out.resize(before);   // the child gave nothing: no separator either
                    else first = false;
                }
                break;
            }
            default:
                break;
        }
    }

    [[nodiscard]] static constexpr std::string unique(std::string base, std::vector<std::string>& taken) {
        std::string s = base;
        for (std::uint32_t n = 1; std::find(taken.begin(), taken.end(), s) != taken.end(); ++n) {
            s = base + std::string(Slug::separator) + write_decimal(n);
        }
        taken.push_back(s);
        return s;
    }
    [[nodiscard]] static constexpr std::string write_decimal(std::uint32_t n) {
        std::string s;
        do {
            s.insert(s.begin(), static_cast<char>('0' + n % 10));
            n /= 10;
        } while (n);
        return s;
    }

    constexpr void close(section& s, std::uint32_t end) const {
        s.end = end;
        s.last_line = end > s.begin ? line_of(end - 1) : s.first_line;
    }

    /// Where the separator at the end of `s` starts: its last line ending and
    /// the blank (whitespace-only) lines before it.
    [[nodiscard]] static constexpr std::size_t trailing_separator(std::string_view s) noexcept {
        std::size_t cut = s.size();
        const auto line_end_before = [&](std::size_t at) -> std::size_t {   // start of a line ending that ends at `at`, or npos
            if (at > 0 && s[at - 1] == '\n') return at >= 2 && s[at - 2] == '\r' ? at - 2 : at - 1;
            if (at > 0 && s[at - 1] == '\r') return at - 1;
            return std::string_view::npos;
        };
        const std::size_t last = line_end_before(cut);
        if (last == std::string_view::npos) return cut;
        cut = last;
        for (;;) {   // whole blank lines before it
            std::size_t k = cut;
            while (k > 0 && (s[k - 1] == ' ' || s[k - 1] == '\t')) --k;
            const std::size_t prev = line_end_before(k);
            if (prev == std::string_view::npos) break;
            cut = prev;
        }
        return cut;
    }
};

using document = basic_document<>;

}  // namespace pygim::pathlike::markdown
