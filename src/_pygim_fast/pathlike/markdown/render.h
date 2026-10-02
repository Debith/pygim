#pragma once
// pathlike/markdown/render.h — a block's content as text, and the tree as HTML.
//
// Pybind-free and constexpr. The content functions turn a block's segments
// (tree.h) back into the text the spec says the block holds; the HTML
// renderer writes what CommonMark's reference renderer writes (commonmark.js
// lib/render/html.js; GFM tables and task items as cmark-gfm writes them),
// which is how tests/unittests/test_pathlike_markdown.py checks the parser
// against the spec's own examples.
//
//     source "> *hi*\n"   ->   html  "<blockquote>\n<p><em>hi</em></p>\n</blockquote>\n"

#include <cstdint>
#include <string>
#include <string_view>

#include "blocks.h"
#include "inlines.h"
#include "syntax.h"
#include "tree.h"

namespace pygim::pathlike::markdown {

/// The text of a paragraph or heading: its lines joined by '\n', trimmed.
///     "  a \n b  " -> "a \n b"
[[nodiscard]] constexpr std::string leaf_text(std::string_view src, const tree& t, const block& b) {
    std::string out;
    for (std::uint32_t k = 0; k < b.nseg; ++k) {
        const segment& s = t.segments[b.seg + k];
        if (k) out.push_back('\n');
        out.append(s.pad, ' ');
        out += src.substr(s.begin, s.end - s.begin);
    }
    return std::string(trim_whitespace(out));
}

/// The literal text of a code or HTML block: every line with its pad, each
/// ended by '\n' (code) or joined by '\n' (HTML).
[[nodiscard]] constexpr std::string literal_text(std::string_view src, const tree& t, const block& b, bool terminate) {
    std::string out;
    for (std::uint32_t k = 0; k < b.nseg; ++k) {
        const segment& s = t.segments[b.seg + k];
        if (k && !terminate) out.push_back('\n');
        out.append(s.pad, ' ');
        out += src.substr(s.begin, s.end - s.begin);
        if (terminate) out.push_back('\n');
    }
    return out;
}

/// A fenced code block's info string, unescaped: "python title=\"x\"".
[[nodiscard]] constexpr std::string info_text(std::string_view src, const block& b) {
    if (!b.fenced) return {};
    return unescape(trim_whitespace(src.substr(b.info.begin, b.info.end - b.info.begin)));
}

/// A table cell's text: `\|` is a pipe inside a cell.
[[nodiscard]] constexpr std::string cell_text(std::string_view src, const segment& s) {
    std::string out;
    const std::string_view v = src.substr(s.begin, s.end - s.begin);
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '|') continue;
        out.push_back(v[i]);
    }
    return out;
}

template <Dialect D = gfm, class Scan = default_scan>
class basic_html_renderer {
public:
    /// HTML for block `root` (the document by default) and everything inside it.
    [[nodiscard]] constexpr std::string render(std::string_view src, const tree& t, std::uint32_t root = 0) {
        m_src = src;
        m_t = &t;
        m_out.clear();
        m_last = '\n';
        render_block(root);
        return std::move(m_out);
    }

private:
    std::string_view m_src;
    const tree* m_t = nullptr;
    std::string m_out;
    char m_last = '\n';
    int m_disable_tags = 0;

    constexpr void lit(std::string_view s) {
        if (s.empty()) return;
        m_out += s;
        m_last = s.back();
    }
    constexpr void esc(std::string_view s) {
        if (s.empty()) return;
        escape_html(s, m_out);
        m_last = m_out.back();
    }
    constexpr void cr() {
        if (m_last != '\n') lit("\n");
    }
    constexpr void tag(std::string_view s) {
        if (m_disable_tags > 0) return;
        m_out.push_back('<');
        m_out += s;
        m_out.push_back('>');
        m_last = '>';
    }

    [[nodiscard]] constexpr const block& at(std::uint32_t i) const { return m_t->blocks[i]; }

    [[nodiscard]] static constexpr bool is_container(kind k) noexcept {
        return k == kind::document || k == kind::quote || k == kind::list || k == kind::item;
    }

    /// Depth-first over `root` without recursion (any nesting depth is safe):
    /// containers are entered and left, leaves rendered whole.
    constexpr void render_block(std::uint32_t root) {
        if (!is_container(at(root).type)) {
            leaf(root);
            return;
        }
        enter(root);
        std::uint32_t cur = at(root).first;
        bool entering = true;
        if (cur == none) {
            leave(root);
            return;
        }
        while (cur != root) {
            const block& b = at(cur);
            if (entering) {
                if (is_container(b.type)) {
                    enter(cur);
                    if (b.first != none) {
                        cur = b.first;
                        continue;
                    }
                    leave(cur);
                } else {
                    leaf(cur);
                }
            } else {
                leave(cur);
            }
            if (b.next != none) {
                cur = b.next;
                entering = true;
            } else {
                cur = b.parent;
                entering = false;
            }
        }
        leave(root);
    }

    constexpr void enter(std::uint32_t i) {
        const block& b = at(i);
        switch (b.type) {
            case kind::quote:
                cr();
                tag("blockquote");
                cr();
                break;
            case kind::list:
                cr();
                if (!b.ordered) tag("ul");
                else if (b.start != 1) tag("ol start=\"" + to_decimal(b.start) + "\"");
                else tag("ol");
                cr();
                break;
            case kind::item:
                tag("li");
                if (b.task >= 0) lit(b.task ? "<input type=\"checkbox\" checked=\"\" disabled=\"\" /> " : "<input type=\"checkbox\" disabled=\"\" /> ");
                break;
            default:
                break;
        }
    }

    constexpr void leave(std::uint32_t i) {
        const block& b = at(i);
        switch (b.type) {
            case kind::quote:
                cr();
                tag("/blockquote");
                cr();
                break;
            case kind::list:
                cr();
                tag(b.ordered ? "/ol" : "/ul");
                cr();
                break;
            case kind::item:
                tag("/li");
                cr();
                break;
            default:
                break;
        }
    }

    constexpr void leaf(std::uint32_t i) {
        const block& b = at(i);
        switch (b.type) {
            case kind::paragraph: {
                const bool tight = b.parent != none && at(b.parent).type == kind::item && at(b.parent).tight;
                if (tight) {
                    inlines(leaf_text(m_src, *m_t, b));
                } else {
                    cr();
                    tag("p");
                    inlines(leaf_text(m_src, *m_t, b));
                    tag("/p");
                    cr();
                }
                break;
            }
            case kind::heading: {
                const std::string h = "h" + to_decimal(b.level);
                cr();
                tag(h);
                inlines(leaf_text(m_src, *m_t, b));
                tag("/" + h);
                cr();
                break;
            }
            case kind::code: {
                cr();
                tag("pre");
                const std::string info = info_text(m_src, b);
                std::size_t w = 0;
                while (w < info.size() && !is_ascii_whitespace(info[w])) ++w;
                if (w > 0) tag("code class=\"language-" + escape_html(std::string_view(info).substr(0, w)) + "\"");
                else tag("code");
                esc(literal_text(m_src, *m_t, b, true));
                tag("/code");
                tag("/pre");
                cr();
                break;
            }
            case kind::html:
                cr();
                lit(literal_text(m_src, *m_t, b, false));
                cr();
                break;
            case kind::thematic_break:
                cr();
                tag("hr /");
                cr();
                break;
            case kind::table:
                table(b);
                break;
            default:   // front matter and definitions render nothing
                break;
        }
    }

    constexpr void table(const block& b) {
        constexpr std::string_view names[] = {"", " align=\"left\"", " align=\"center\"", " align=\"right\""};
        const auto row = [&](std::uint32_t r, std::string_view cell) {
            tag("tr");
            cr();
            for (std::uint32_t c = 0; c < b.columns; ++c) {
                const std::string_view a = names[static_cast<int>(m_t->aligns[b.align + c])];
                tag(std::string(cell) + std::string(a));
                inlines(std::string(trim_whitespace(cell_text(m_src, m_t->segments[b.seg + r * b.columns + c]))));
                tag("/" + std::string(cell));
                cr();
            }
            tag("/tr");
            cr();
        };
        cr();
        tag("table");
        cr();
        tag("thead");
        cr();
        row(0, "th");
        tag("/thead");
        cr();
        if (b.rows > 1) {
            tag("tbody");
            cr();
            for (std::uint32_t r = 1; r < b.rows; ++r) row(r, "td");
            tag("/tbody");
            cr();
        }
        tag("/table");
        cr();
    }

    constexpr void inlines(std::string_view text) {
        const inline_tree it = basic_inline_parser<D, Scan>{}.parse(text, *m_t);
        inline_children(it, 0);
    }

    /// Every inline under `root`, depth-first without recursion.
    constexpr void inline_children(const inline_tree& it, std::uint32_t root) {
        std::uint32_t cur = it.nodes[root].first;
        bool entering = true;
        if (cur == none) return;
        while (cur != root) {
            const inline_node& n = it.nodes[cur];
            if (entering) {
                inline_enter(n);
                if (n.first != none) {
                    cur = n.first;
                    continue;
                }
            }
            inline_leave(n);
            if (n.next != none) {
                cur = n.next;
                entering = true;
            } else {
                cur = n.parent;
                entering = false;
            }
        }
    }

    constexpr void inline_enter(const inline_node& n) {
        switch (n.type) {
            case inline_kind::text: esc(n.literal); break;
            case inline_kind::softbreak: lit("\n"); break;
            case inline_kind::linebreak:
                tag("br /");
                cr();
                break;
            case inline_kind::code:
                tag("code");
                esc(n.literal);
                tag("/code");
                break;
            case inline_kind::html: lit(n.literal); break;
            case inline_kind::emph: tag("em"); break;
            case inline_kind::strong: tag("strong"); break;
            case inline_kind::del: tag("del"); break;
            case inline_kind::link: {
                std::string a = "a href=\"" + escape_html(n.destination) + "\"";
                if (!n.title.empty()) a += " title=\"" + escape_html(n.title) + "\"";
                tag(a);
                break;
            }
            case inline_kind::image:
                if (m_disable_tags == 0) lit("<img src=\"" + escape_html(n.destination) + "\" alt=\"");
                ++m_disable_tags;
                break;
            case inline_kind::root: break;
        }
    }

    constexpr void inline_leave(const inline_node& n) {
        switch (n.type) {
            case inline_kind::emph: tag("/em"); break;
            case inline_kind::strong: tag("/strong"); break;
            case inline_kind::del: tag("/del"); break;
            case inline_kind::link: tag("/a"); break;
            case inline_kind::image:
                --m_disable_tags;
                if (m_disable_tags == 0) {
                    if (!n.title.empty()) lit("\" title=\"" + escape_html(n.title));
                    lit("\" />");
                }
                break;
            default: break;
        }
    }

    [[nodiscard]] static constexpr std::string to_decimal(std::uint32_t v) {
        std::string s;
        do {
            s.insert(s.begin(), static_cast<char>('0' + v % 10));
            v /= 10;
        } while (v);
        return s;
    }
};

using html_renderer = basic_html_renderer<>;

}  // namespace pygim::pathlike::markdown
