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
// Two layers. `document_core` is what does not depend on the policies — the
// source, the tree, a block's lines and literal text, finding, the edits —
// so it is compiled once, not once per dialect and slug rule.
// `basic_document<Dialect, Slug, Scan>` adds what does: inline text, HTML,
// slugs and sections. (any_document.h chooses the policies at run time.)
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
// caches (headings, sections, the code-point table), which fill on first
// use; a caller sharing one document across threads fills them first (or
// holds a lock, as the Python adapter does through the GIL).

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
    std::uint32_t first_child = none, next_sibling = none;   // its subsections, as a chain of indices
    std::uint32_t slug_index = 0;   // index into headings()
};

/// The policy-free half of a document.
class document_core {
public:
    constexpr document_core(std::string source, bool front_matter, tree parsed)
        : m_source(std::move(source)), m_front_matter(front_matter), m_tree(std::move(parsed)) {}

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
    /// The code of a code block, every line ended by '\n'; its info string, unescaped.
    [[nodiscard]] constexpr std::string code(std::uint32_t i) const { return literal_text(m_source, m_tree, at(i), true); }
    [[nodiscard]] constexpr std::string info(std::uint32_t i) const { return info_text(m_source, at(i)); }
    /// What definition block `i` defines; std::invalid_argument for a block of another kind.
    [[nodiscard]] constexpr const definition& definition_of(std::uint32_t i) const {
        if (at(i).type != kind::definition) throw not_a(i, kind::definition);
        return m_tree.definitions[at(i).def];
    }
    /// The raw text of an HTML block; the body of the front matter.
    [[nodiscard]] constexpr std::string raw(std::uint32_t i) const { return literal_text(m_source, m_tree, at(i), false); }

    /// The front matter block, or none.
    [[nodiscard]] constexpr std::uint32_t front_matter() const noexcept {
        const std::uint32_t first = at(0).first;
        return first != none && at(first).type == kind::front_matter ? first : none;
    }
    /// The blocks directly under block `i` (the document: 0).
    [[nodiscard]] constexpr std::vector<std::uint32_t> children(std::uint32_t i) const {
        std::vector<std::uint32_t> out;
        for (std::uint32_t c = at(i).first; c != none; c = at(c).next) out.push_back(c);
        return out;
    }
    /// Every block below the document, depth-first (a container before what it holds).
    [[nodiscard]] constexpr std::vector<std::uint32_t> walk() const {
        std::vector<std::uint32_t> out;
        walk([&](std::uint32_t i) { out.push_back(i); });
        return out;
    }
    /// The blocks of one kind, depth-first.
    [[nodiscard]] constexpr std::vector<std::uint32_t> find(kind k) const {
        std::vector<std::uint32_t> out;
        walk([&](std::uint32_t i) {
            if (at(i).type == k) out.push_back(i);
        });
        return out;
    }

    /// The 1-based line holding byte `offset` (the last line for the end of the source).
    [[nodiscard]] constexpr std::uint32_t line_of(std::uint32_t offset) const noexcept {
        const auto& ls = m_tree.line_starts;
        return static_cast<std::uint32_t>(std::upper_bound(ls.begin(), ls.end(), offset) - ls.begin());
    }
    /// The code points before byte `offset` — the offset a caller indexing by
    /// character uses (Python's str) — through a per-line table built on first use.
    [[nodiscard]] constexpr std::size_t code_points(std::uint32_t offset) const {
        const auto& ls = m_tree.line_starts;
        if (!m_code_points) {
            std::vector<std::size_t> table;
            table.reserve(ls.size() + 1);
            std::size_t n = 0, at = 0;
            for (std::uint32_t start : ls) {
                n += count_code_points(std::string_view(m_source).substr(at, start - at));
                at = start;
                table.push_back(n);
            }
            table.push_back(n + count_code_points(std::string_view(m_source).substr(at)));   // the end of the source
            m_code_points = std::move(table);
        }
        if (offset >= m_source.size()) return m_code_points->back();
        const auto it = std::upper_bound(ls.begin(), ls.end(), offset);
        if (it == ls.begin()) return count_code_points(std::string_view(m_source).substr(0, offset));
        const std::size_t line = static_cast<std::size_t>(it - ls.begin()) - 1;
        return (*m_code_points)[line] + count_code_points(std::string_view(m_source).substr(ls[line], offset - ls[line]));
    }

    /// The bytes an edit of block `i` replaces: its whole lines. Only a
    /// top-level block has them to itself — a block inside a quote or a list
    /// shares its lines with the container's markers — so any other block is
    /// refused (std::invalid_argument, naming it and its container).
    [[nodiscard]] constexpr std::pair<std::uint32_t, std::uint32_t> region(std::uint32_t i) const {
        const block& b = at(i);
        if (b.parent != 0) {
            throw std::invalid_argument("only a top-level block or a section can be replaced, not a " +
                                        std::string(kind_name(b.type)) + " inside a " +
                                        std::string(kind_name(at(b.parent).type)) + " (lines " +
                                        decimal(b.first_line) + "-" + decimal(b.last_line) + ")");
        }
        return {b.begin, b.end};
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
    /// The source with its front matter replaced by `text` (serialised, fences
    /// included), added after any byte-order mark when there is none, or
    /// removed when `text` is empty. The body's bytes do not change.
    [[nodiscard]] constexpr std::string with_front_matter(std::string_view text) const {
        const std::uint32_t fm = front_matter();
        if (fm != none) return splice(at(fm).begin, at(fm).end, text);
        const std::size_t after_bom = std::string_view(m_source).starts_with("\xEF\xBB\xBF") ? 3 : 0;
        std::string out = m_source.substr(0, after_bom);
        out += text;
        out += std::string_view(m_source).substr(after_bom);
        return out;
    }

    /// The heap bytes of the source, the tree and the code-point table, exactly.
    [[nodiscard]] std::size_t bytes() const noexcept {
        return m_source.capacity() + 1 + m_tree.bytes() + (m_code_points ? m_code_points->capacity() * sizeof(std::size_t) : 0);
    }

protected:
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

    /// The error for asking block `i` what only a block of kind `k` has.
    [[nodiscard]] constexpr std::invalid_argument not_a(std::uint32_t i, kind k) const {
        return std::invalid_argument("lines " + decimal(at(i).first_line) + "-" + decimal(at(i).last_line) + " are a " +
                                     std::string(kind_name(at(i).type)) + ", not a " + std::string(kind_name(k)));
    }

private:
    std::string m_source;
    bool m_front_matter = true;
    tree m_tree;
    mutable std::optional<std::vector<std::size_t>> m_code_points;   // per line start, then the end

    [[nodiscard]] static constexpr std::size_t count_code_points(std::string_view v) noexcept {
        std::size_t n = 0;
        for (char c : v) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
        return n;
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

/// A document under its policies: what the dialect, the slug rule and the scan decide.
template <Dialect D = gfm, SlugPolicy Slug = github_slug, class Scan = default_scan>
class basic_document : public document_core {
public:
    using dialect = D;
    using slug_policy = Slug;

    /// Parses `source`. `front_matter` recognises `---`/`+++` front matter at the start.
    constexpr explicit basic_document(std::string source, bool front_matter = true)
        : document_core(parsed(std::move(source), front_matter)) {}

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
    /// The first line of plain(i) — what a reader sees first — from the first
    /// block below `i` that gives any text, without building the rest: a
    /// preview of a 2 MB list costs its first item.
    [[nodiscard]] constexpr std::string first_line(std::uint32_t i) const {
        std::vector<std::uint32_t> todo{i};   // pre-order, children pushed in reverse
        while (!todo.empty()) {
            const std::uint32_t b = todo.back();
            todo.pop_back();
            if (holds_blocks(at(b).type)) {
                const std::size_t mark = todo.size();
                for (std::uint32_t c = at(b).first; c != none; c = at(c).next) todo.push_back(c);
                std::reverse(todo.begin() + static_cast<std::ptrdiff_t>(mark), todo.end());
                continue;
            }
            std::string text;
            append_plain_leaf(b, text);
            if (!text.empty()) return text.substr(0, text.find('\n'));
        }
        return {};
    }

    /// HTML for the whole document, or for block `i`, as CommonMark's reference renderer writes it.
    [[nodiscard]] constexpr std::string html(std::uint32_t i = 0) const {
        return basic_html_renderer<D, Scan>{}.render(source(), structure(), i);
    }

    /// Every heading, in document order (those inside quotes and lists too), with unique slugs.
    [[nodiscard]] constexpr const std::vector<heading_info>& headings() const {
        if (!m_headings) {
            std::vector<heading_info> out;
            typename Slug::anchors anchors;   // unique by the slug rule's own reference
            walk([&](std::uint32_t i) {
                if (at(i).type != kind::heading) return;
                heading_info h;
                h.block = i;
                h.title = plain(i);
                std::replace(h.title.begin(), h.title.end(), '\n', ' ');
                h.slug = anchors.next(slug<Slug>(h.title));
                out.push_back(std::move(h));
            });
            m_heading_of.assign(structure().blocks.size(), none);   // block -> its index in headings()
            for (std::uint32_t k = 0; k < out.size(); ++k) m_heading_of[out[k].block] = k;
            m_headings = std::move(out);
        }
        return *m_headings;
    }
    /// The heading_info of heading block `i`; std::invalid_argument for a block of another kind.
    [[nodiscard]] constexpr const heading_info& heading(std::uint32_t i) const {
        const std::vector<heading_info>& hs = headings();
        if (m_heading_of[i] == none) throw not_a(i, kind::heading);
        return hs[m_heading_of[i]];
    }

    /// The sections of the top-level headings, in document order.
    [[nodiscard]] constexpr const std::vector<section>& sections() const {
        if (!m_sections) {
            const std::vector<heading_info>& hs = headings();
            std::vector<section> out;
            std::vector<std::uint32_t> open;         // indices of sections still open, deepest last
            std::vector<std::uint32_t> last_child;   // per section: its last subsection so far
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
                const auto at_index = static_cast<std::uint32_t>(out.size());
                if (s.parent != none) {   // append to the parent's chain of subsections
                    std::uint32_t& link = last_child[s.parent] == none ? out[s.parent].first_child
                                                                       : out[last_child[s.parent]].next_sibling;
                    link = at_index;
                    last_child[s.parent] = at_index;
                }
                open.push_back(at_index);
                out.push_back(s);
                last_child.push_back(none);
            }
            for (std::uint32_t o : open) close(out[o], static_cast<std::uint32_t>(source().size()));
            m_sections = std::move(out);
        }
        return *m_sections;
    }
    /// The section whose slug, else whose title, is `key`; none when there is none.
    [[nodiscard]] constexpr std::uint32_t section_of(std::string_view key) const {
        const auto& ss = sections();
        const auto& hs = headings();
        const auto by = [&](std::string heading_info::*field) {
            const auto it = std::find_if(ss.begin(), ss.end(), [&](const section& s) { return hs[s.slug_index].*field == key; });
            return it == ss.end() ? none : static_cast<std::uint32_t>(it - ss.begin());
        };
        const std::uint32_t by_slug = by(&heading_info::slug);
        return by_slug != none ? by_slug : by(&heading_info::title);
    }
    /// The subsections of section `s`, in order.
    [[nodiscard]] constexpr std::vector<std::uint32_t> subsections(std::uint32_t s) const {
        std::vector<std::uint32_t> out;
        for (std::uint32_t c = sections()[s].first_child; c != none; c = sections()[c].next_sibling) out.push_back(c);
        return out;
    }
    /// The top-level blocks of section `s` after its heading, up to its end (subsection headings included).
    [[nodiscard]] constexpr std::vector<std::uint32_t> section_blocks(std::uint32_t s) const {
        const section& x = sections()[s];
        std::vector<std::uint32_t> out;
        for (std::uint32_t c = at(x.heading).next; c != none && at(c).begin < x.end; c = at(c).next) out.push_back(c);
        return out;
    }
    /// The bytes an edit of section `s` replaces: its heading up to the next section's heading.
    [[nodiscard]] constexpr std::pair<std::uint32_t, std::uint32_t> section_region(std::uint32_t s) const {
        return {sections()[s].begin, sections()[s].end};
    }

    /// The heap bytes this document holds, exactly: document_core's, plus the
    /// heading and section caches once filled.
    [[nodiscard]] std::size_t bytes() const noexcept {
        std::size_t n = document_core::bytes() + m_heading_of.capacity() * sizeof(std::uint32_t);
        if (m_headings) {
            n += m_headings->capacity() * sizeof(heading_info);
            for (const heading_info& h : *m_headings) n += h.title.capacity() + h.slug.capacity();
        }
        if (m_sections) n += m_sections->capacity() * sizeof(section);
        return n;
    }

private:
    mutable std::optional<std::vector<heading_info>> m_headings;
    mutable std::vector<std::uint32_t> m_heading_of;   // block -> index in headings(), none for other blocks
    mutable std::optional<std::vector<section>> m_sections;

    /// The core of `source` parsed under D (the tree holds offsets, so the source moves after).
    [[nodiscard]] static constexpr document_core parsed(std::string source, bool front_matter) {
        tree t = basic_block_parser<D>{}.parse(source, front_matter);
        return document_core(std::move(source), front_matter, std::move(t));
    }

    [[nodiscard]] constexpr std::string inline_plain(std::string_view text) const {
        const inline_tree it = basic_inline_parser<D, Scan>{}.parse(text, structure());
        std::string out;
        append_plain(it, 0, out);
        return out;
    }

    [[nodiscard]] static constexpr bool holds_blocks(kind k) noexcept {
        return k == kind::document || k == kind::quote || k == kind::list || k == kind::item;
    }

    /// A leaf block's plain text: inline markup resolved, a table's rows tab-separated.
    constexpr void append_plain_leaf(std::uint32_t i, std::string& out) const {
        const block& b = at(i);
        switch (b.type) {
            case kind::paragraph:
            case kind::heading:
                out += inline_plain(inline_source(i));
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
            default:
                break;
        }
    }

    /// Block `i`'s plain text: its blocks' texts, a blank line between two that
    /// give any. Iterative, like every walk here: a document nests as deep as
    /// its input (a 5 KB line of `>` is 5,000 quotes), deeper than a thread's stack.
    constexpr void append_plain_block(std::uint32_t i, std::string& out) const {
        if (!holds_blocks(at(i).type)) {
            append_plain_leaf(i, out);
            return;
        }
        // An open container: its next child, whether it has given text yet, and
        // where its own text began in its parent (`before`, then `mark` after the separator).
        struct open { std::uint32_t child; bool first; std::size_t before, mark; };
        std::vector<open> stack{{at(i).first, true, 0, 0}};
        const auto ended = [&](std::size_t before, std::size_t mark) {   // a child of stack.back() is complete
            if (out.size() == mark) out.resize(before);   // it gave nothing: no separator either
            else stack.back().first = false;
        };
        while (!stack.empty()) {
            const std::uint32_t c = stack.back().child;
            if (c == none) {
                const open done = stack.back();
                stack.pop_back();
                if (!stack.empty()) ended(done.before, done.mark);
                continue;
            }
            stack.back().child = at(c).next;
            const std::size_t before = out.size();
            if (!stack.back().first) out += "\n\n";
            const std::size_t mark = out.size();
            if (holds_blocks(at(c).type)) {
                stack.push_back({at(c).first, true, before, mark});
            } else {
                append_plain_leaf(c, out);
                ended(before, mark);
            }
        }
    }

    constexpr void close(section& s, std::uint32_t end) const {
        s.end = end;
        s.last_line = end > s.begin ? line_of(end - 1) : s.first_line;
    }
};

using document = basic_document<>;

}  // namespace pygim::pathlike::markdown
