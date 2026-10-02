#pragma once
// pathlike/markdown/inlines.h — the inline parser: a leaf's text -> inline nodes.
//
// Pybind-free and constexpr. The algorithm is the CommonMark spec's (its
// appendix, "Phase 2: inline structure") as commonmark.js implements it: a
// left-to-right pass appends text, code spans, autolinks, raw HTML and
// entities, and pushes every `*`/`_` run onto a delimiter stack and every
// `[`/`![` onto a bracket stack; a `]` resolves a link or image, and
// process_emphasis pairs the delimiters into emphasis. GFM adds `~`/`~~`
// strikethrough, paired run with run of the same length.
//
// Text runs are found with the stop index (scan.h): the parser jumps from
// one byte that can start markup to the next, so plain text costs one
// count-trailing-zeros per run instead of a test per byte.
//
//     parse("*a* [b](/u)")  ->  root [emph [text "a"], text " ", link(/u) [text "b"]]
//
// Each inline block (paragraph, heading, table cell) is parsed on its own,
// when its inlines are first needed (document.h); link references resolve
// against the tree's definitions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "blocks.h"
#include "scan.h"
#include "syntax.h"
#include "tree.h"
#include "unicode.h"

namespace pygim::pathlike::markdown {

enum class inline_kind : std::uint8_t { root, text, softbreak, linebreak, code, html, emph, strong, del, link, image };

struct inline_node {
    inline_kind type = inline_kind::text;
    std::uint32_t parent = none, first = none, last = none, prev = none, next = none;
    std::string literal;      // text, code, html
    std::string destination;  // link, image
    std::string title;        // link, image
};

/// The inlines of one leaf; nodes[0] is the root.
struct inline_tree {
    std::vector<inline_node> nodes;
};

template <Dialect D = gfm, class Scan = default_scan>
class basic_inline_parser {
public:
    /// The inlines of `subject` (a leaf's text, lines joined by '\n', trimmed).
    [[nodiscard]] constexpr inline_tree parse(std::string_view subject, const tree& refs) {
        m_s = subject;
        m_pos = 0;
        m_refs = &refs;
        m_stops = basic_stop_index<Scan>(subject);
        m_out = inline_tree{};
        m_out.nodes.emplace_back().type = inline_kind::root;
        m_delims.clear();
        m_brackets.clear();
        m_delim_top = m_bracket_top = m_deactivated_to = none;
        m_backticks_scanned = false;
        m_html_misses = {};
        while (parse_inline()) {
        }
        process_emphasis(none);
        return std::move(m_out);
    }

private:
    struct delimiter {
        char cc = 0;
        std::uint32_t numdelims = 0, origdelims = 0, node = none, prev = none, next = none;
        bool can_open = false, can_close = false;
    };
    struct bracket {
        std::uint32_t node = none, prev = none, prev_delim = none;
        std::size_t index = 0;
        bool image = false, active = true, bracket_after = false;
    };

    std::string_view m_s;
    std::size_t m_pos = 0;
    const tree* m_refs = nullptr;
    basic_stop_index<Scan> m_stops;
    inline_tree m_out;
    std::vector<delimiter> m_delims;
    std::vector<bracket> m_brackets;
    std::uint32_t m_delim_top = none, m_bracket_top = none;
    std::uint32_t m_deactivated_to = none;   // every link opener at or below this index is already inactive
    bool m_backticks_scanned = false;
    std::array<std::size_t, 64> m_last_ticks{};   // the last run of each length (cache for unmatched code spans)
    html_misses m_html_misses;                    // where searches for the end of raw HTML already failed

    [[nodiscard]] constexpr inline_node& node(std::uint32_t i) { return m_out.nodes[i]; }

    // ── the node list ──
    constexpr std::uint32_t make(inline_kind t, std::string literal = {}) {
        const auto i = static_cast<std::uint32_t>(m_out.nodes.size());
        inline_node n;
        n.type = t;
        n.literal = std::move(literal);
        m_out.nodes.push_back(std::move(n));
        return i;
    }
    constexpr void append_child(std::uint32_t parent, std::uint32_t child) {
        inline_node& c = node(child);
        c.parent = parent;
        c.next = none;
        c.prev = node(parent).last;
        if (c.prev != none) node(c.prev).next = child;
        else node(parent).first = child;
        node(parent).last = child;
    }
    constexpr std::uint32_t append(inline_kind t, std::string literal = {}) {
        const std::uint32_t i = make(t, std::move(literal));
        append_child(0, i);
        return i;
    }
    constexpr std::uint32_t text(std::string_view s) { return append(inline_kind::text, std::string(s)); }
    constexpr void unlink(std::uint32_t i) {
        inline_node& n = node(i);
        if (n.parent == none) return;
        if (n.prev != none) node(n.prev).next = n.next;
        else node(n.parent).first = n.next;
        if (n.next != none) node(n.next).prev = n.prev;
        else node(n.parent).last = n.prev;
        n.parent = n.prev = n.next = none;
    }
    constexpr void insert_after(std::uint32_t at, std::uint32_t i) {
        inline_node& n = node(i);
        n.parent = node(at).parent;
        n.prev = at;
        n.next = node(at).next;
        if (n.next != none) node(n.next).prev = i;
        else node(n.parent).last = i;
        node(at).next = i;
    }
    /// Moves every sibling after `from` (up to, not including, `until`) under `into`.
    constexpr void adopt(std::uint32_t into, std::uint32_t from, std::uint32_t until) {
        std::uint32_t t = node(from).next;
        while (t != none && t != until) {
            const std::uint32_t next = node(t).next;
            unlink(t);
            append_child(into, t);
            t = next;
        }
    }

    [[nodiscard]] constexpr bool special(char c) const noexcept {
        return c == '~' ? D::strikethrough : stop_table[static_cast<unsigned char>(c)];
    }

    // ── one step ──
    constexpr bool parse_inline() {
        if (m_pos >= m_s.size()) return false;
        const char c = m_s[m_pos];
        bool res = false;
        switch (c) {
            case '\n': res = parse_newline(); break;
            case '\\': res = parse_backslash(); break;
            case '`': res = parse_backticks(); break;
            case '*': case '_': res = handle_delim(c); break;
            case '~': res = D::strikethrough ? handle_delim(c) : parse_string(); break;
            case '[': res = parse_open_bracket(); break;
            case '!': res = parse_bang(); break;
            case ']': res = parse_close_bracket(); break;
            case '<': res = parse_autolink() || parse_html(); break;
            case '&': res = parse_entity(); break;
            default: res = parse_string(); break;
        }
        if (!res) {
            m_pos += 1;
            text(m_s.substr(m_pos - 1, 1));
        }
        return true;
    }

    constexpr bool parse_string() {
        std::size_t end = m_stops.next(m_pos + 1);
        while (end != basic_stop_index<Scan>::npos && !special(m_s[end])) end = m_stops.next(end + 1);
        if (end == basic_stop_index<Scan>::npos) end = m_s.size();
        text(m_s.substr(m_pos, end - m_pos));
        m_pos = end;
        return true;
    }

    constexpr bool parse_newline() {
        m_pos += 1;
        const std::uint32_t last = node(0).last;
        if (last != none && node(last).type == inline_kind::text && !node(last).literal.empty() &&
            node(last).literal.back() == ' ') {
            std::string& lit = node(last).literal;
            const bool hard = lit.size() >= 2 && lit[lit.size() - 2] == ' ';
            while (!lit.empty() && lit.back() == ' ') lit.pop_back();
            append(hard ? inline_kind::linebreak : inline_kind::softbreak);
        } else {
            append(inline_kind::softbreak);
        }
        while (m_pos < m_s.size() && m_s[m_pos] == ' ') ++m_pos;
        return true;
    }

    constexpr bool parse_backslash() {
        m_pos += 1;
        if (m_pos < m_s.size() && m_s[m_pos] == '\n') {
            m_pos += 1;
            append(inline_kind::linebreak);
        } else if (m_pos < m_s.size() && is_ascii_punctuation(m_s[m_pos])) {
            text(m_s.substr(m_pos, 1));
            m_pos += 1;
        } else {
            text("\\");
        }
        return true;
    }

    [[nodiscard]] constexpr std::size_t run_length(std::size_t i, char c) const noexcept {
        std::size_t n = 0;
        while (i + n < m_s.size() && m_s[i + n] == c) ++n;
        return n;
    }

    constexpr bool parse_backticks() {
        const std::size_t start = m_pos;
        const std::size_t n = run_length(start, '`');
        const std::size_t after = start + n;
        if (!m_backticks_scanned) {   // the last run of each length, once per subject
            m_backticks_scanned = true;
            m_last_ticks.fill(0);
            for (std::size_t j = m_stops.next(0); j != basic_stop_index<Scan>::npos;) {
                if (m_s[j] != '`') {
                    j = m_stops.next(j + 1);
                    continue;
                }
                const std::size_t len = run_length(j, '`');
                if (len < m_last_ticks.size()) m_last_ticks[len] = j + 1;   // +1: 0 means "never"
                j = m_stops.next(j + len);
            }
        }
        const bool may_close = n >= m_last_ticks.size() || m_last_ticks[n] > after;
        for (std::size_t j = may_close ? m_stops.next(after) : basic_stop_index<Scan>::npos; j != basic_stop_index<Scan>::npos;) {
            if (m_s[j] != '`') {
                j = m_stops.next(j + 1);
                continue;
            }
            const std::size_t len = run_length(j, '`');
            if (len == n) {
                std::string content(m_s.substr(after, j - after));
                for (char& c : content) {
                    if (c == '\n') c = ' ';
                }
                if (content.size() >= 2 && content.front() == ' ' && content.back() == ' ' &&
                    content.find_first_not_of(' ') != std::string::npos) {
                    content = content.substr(1, content.size() - 2);
                }
                append(inline_kind::code, std::move(content));
                m_pos = j + n;
                return true;
            }
            j = m_stops.next(j + len);
        }
        m_pos = after;
        text(m_s.substr(start, n));
        return true;
    }

    // ── emphasis delimiters ──
    constexpr bool handle_delim(char cc) {
        const std::size_t start = m_pos;
        const std::size_t n = run_length(start, cc);
        if (n == 0) return false;
        const char32_t before = decode_before(m_s, start).cp;
        const char32_t after = start + n < m_s.size() ? decode(m_s, start + n).cp : U'\n';
        const bool after_ws = is_whitespace(after), after_p = is_punctuation(after);
        const bool before_ws = is_whitespace(before), before_p = is_punctuation(before);
        const bool left = !after_ws && (!after_p || before_ws || before_p);
        const bool right = !before_ws && (!before_p || after_ws || after_p);
        bool can_open = left, can_close = right;
        if (cc == '_') {
            can_open = left && (!right || before_p);
            can_close = right && (!left || after_p);
        }
        m_pos += n;
        const std::uint32_t t = text(m_s.substr(start, n));
        if (cc == '~' && n > 2) return true;   // GFM: three or more tildes are text
        if (can_open || can_close) {
            const auto i = static_cast<std::uint32_t>(m_delims.size());
            delimiter d;
            d.cc = cc;
            d.numdelims = d.origdelims = static_cast<std::uint32_t>(n);
            d.node = t;
            d.prev = m_delim_top;
            d.can_open = can_open;
            d.can_close = can_close;
            m_delims.push_back(d);
            if (m_delim_top != none) m_delims[m_delim_top].next = i;
            m_delim_top = i;
        }
        return true;
    }

    constexpr void remove_delimiter(std::uint32_t d) {
        delimiter& x = m_delims[d];
        if (x.prev != none) m_delims[x.prev].next = x.next;
        if (x.next == none) m_delim_top = x.prev;
        else m_delims[x.next].prev = x.prev;
    }

    constexpr void process_emphasis(std::uint32_t bottom) {
        std::array<std::uint32_t, 18> openers_bottom{};
        openers_bottom.fill(bottom);
        std::uint32_t closer = m_delim_top;
        while (closer != none && m_delims[closer].prev != bottom) closer = m_delims[closer].prev;
        while (closer != none) {
            delimiter& c = m_delims[closer];
            if (!c.can_close) {
                closer = c.next;
                continue;
            }
            const std::size_t ob = c.cc == '~' ? 14 + c.origdelims
                                 : (c.cc == '_' ? 2 : 8) + (c.can_open ? 3 : 0) + c.origdelims % 3;
            std::uint32_t opener = c.prev;
            bool found = false;
            while (opener != none && opener != bottom && opener != openers_bottom[ob]) {
                const delimiter& o = m_delims[opener];
                if (c.cc == '~') {
                    if (o.cc == '~' && o.can_open && o.numdelims == c.numdelims) {
                        found = true;
                        break;
                    }
                } else {
                    const bool odd = (c.can_open || o.can_close) && c.origdelims % 3 != 0 &&
                                     (o.origdelims + c.origdelims) % 3 == 0;
                    if (o.cc == c.cc && o.can_open && !odd) {
                        found = true;
                        break;
                    }
                }
                opener = o.prev;
            }
            const std::uint32_t old_closer = closer;
            if (!found) {
                closer = c.next;
            } else {
                delimiter& o = m_delims[opener];
                const std::uint32_t use = c.cc == '~' ? c.numdelims : (c.numdelims >= 2 && o.numdelims >= 2 ? 2 : 1);
                const std::uint32_t oi = o.node, ci = c.node;
                o.numdelims -= use;
                c.numdelims -= use;
                node(oi).literal.resize(node(oi).literal.size() - use);
                node(ci).literal.resize(node(ci).literal.size() - use);
                const std::uint32_t emph = make(c.cc == '~' ? inline_kind::del : use == 1 ? inline_kind::emph : inline_kind::strong);
                adopt(emph, oi, ci);
                insert_after(oi, emph);
                if (o.next != closer) {   // remove the delimiters between opener and closer
                    m_delims[opener].next = closer;
                    m_delims[closer].prev = opener;
                }
                if (m_delims[opener].numdelims == 0) {
                    unlink(oi);
                    remove_delimiter(opener);
                }
                if (m_delims[closer].numdelims == 0) {
                    unlink(ci);
                    const std::uint32_t next = m_delims[closer].next;
                    remove_delimiter(closer);
                    closer = next;
                }
            }
            if (!found) {
                openers_bottom[ob] = m_delims[old_closer].prev;
                if (!m_delims[old_closer].can_open) remove_delimiter(old_closer);
            }
        }
        while (m_delim_top != none && m_delim_top != bottom) remove_delimiter(m_delim_top);
    }

    // ── links and images ──
    constexpr void add_bracket(std::uint32_t n, std::size_t index, bool image) {
        if (m_bracket_top != none) m_brackets[m_bracket_top].bracket_after = true;
        bracket b;
        b.node = n;
        b.prev = m_bracket_top;
        b.prev_delim = m_delim_top;
        b.index = index;
        b.image = image;
        m_bracket_top = static_cast<std::uint32_t>(m_brackets.size());
        m_brackets.push_back(b);
    }
    constexpr bool parse_open_bracket() {
        const std::size_t start = m_pos;
        m_pos += 1;
        add_bracket(text("["), start, false);
        return true;
    }
    constexpr bool parse_bang() {
        const std::size_t start = m_pos;
        m_pos += 1;
        if (m_pos < m_s.size() && m_s[m_pos] == '[') {
            m_pos += 1;
            add_bracket(text("!["), start + 1, true);
        } else {
            text("!");
        }
        return true;
    }

    constexpr bool parse_close_bracket() {
        m_pos += 1;
        const std::size_t start = m_pos;
        if (m_bracket_top == none) {
            text("]");
            return true;
        }
        if (!m_brackets[m_bracket_top].active) {
            text("]");
            m_bracket_top = m_brackets[m_bracket_top].prev;
            return true;
        }
        const bracket opener = m_brackets[m_bracket_top];
        std::string dest, title;
        bool matched = false;
        const std::size_t save = m_pos;
        if (m_pos < m_s.size() && m_s[m_pos] == '(') {   // an inline link
            std::size_t p = skip_spnl(m_s, m_pos + 1);
            if (auto d = link_destination(m_s, p)) {
                p = skip_spnl(m_s, p + d->len);
                std::string t;
                if (p > 0 && is_ascii_whitespace(m_s[p - 1])) {   // a title must follow whitespace
                    if (auto tt = link_title(m_s, p)) {
                        t = std::move(tt->value);
                        p += tt->len;
                    }
                }
                p = skip_spnl(m_s, p);
                if (p < m_s.size() && m_s[p] == ')') {
                    m_pos = p + 1;
                    dest = std::move(d->value);
                    title = std::move(t);
                    matched = true;
                }
            }
            if (!matched) m_pos = save;
        }
        if (!matched) {   // a reference link: full, collapsed or shortcut
            const std::size_t before_label = m_pos;
            const std::size_t n = link_label(m_s, m_pos);
            std::string_view raw;
            if (n > 2) raw = m_s.substr(before_label + 1, n - 2);
            else if (!opener.bracket_after) raw = m_s.substr(opener.index + 1, start - opener.index - 2);
            if (n == 0) m_pos = save;
            else m_pos = before_label + n;
            if (n > 2 || !opener.bracket_after) {
                if (const definition* def = m_refs->lookup(fold_label(raw))) {
                    dest = def->destination;
                    title = def->title;
                    matched = true;
                }
            }
        }
        if (!matched) {
            m_bracket_top = opener.prev;
            m_pos = start;
            text("]");
            return true;
        }
        const std::uint32_t link = make(opener.image ? inline_kind::image : inline_kind::link);
        node(link).destination = std::move(dest);
        node(link).title = std::move(title);
        adopt(link, opener.node, none);
        append_child(0, link);
        process_emphasis(opener.prev_delim);
        m_bracket_top = opener.prev;
        unlink(opener.node);
        if (!opener.image) {   // no links inside links: deactivate the earlier openers
            // Openers at or below m_deactivated_to were deactivated by an earlier
            // link, so the walk stops there: each opener is visited once, and
            // `![[]()` repeated stays linear.
            for (std::uint32_t b = m_bracket_top; b != none && (m_deactivated_to == none || b > m_deactivated_to);
                 b = m_brackets[b].prev) {
                if (!m_brackets[b].image) m_brackets[b].active = false;
            }
            m_deactivated_to = static_cast<std::uint32_t>(m_brackets.size() - 1);
        }
        return true;
    }

    // ── `<`: autolinks and raw HTML; `&`: references ──
    constexpr bool parse_autolink() {
        const std::string_view s = m_s.substr(m_pos);
        // e-mail
        std::size_t i = 1;
        constexpr std::string_view local_extra = ".!#$%&'*+/=?^_`{|}~-";
        while (i < s.size() && (is_ascii_alnum(s[i]) || local_extra.find(s[i]) != std::string_view::npos)) ++i;
        if (i > 1 && i < s.size() && s[i] == '@') {
            std::size_t j = i + 1;
            bool ok = true;
            for (;;) {   // labels: alnum (alnum|-){0,61} alnum, separated by '.'
                const std::size_t label = j;
                if (j >= s.size() || !is_ascii_alnum(s[j])) {
                    ok = false;
                    break;
                }
                while (j < s.size() && (is_ascii_alnum(s[j]) || s[j] == '-') && j - label < 63) ++j;
                if (s[j - 1] == '-') {
                    ok = false;
                    break;
                }
                if (j < s.size() && s[j] == '.') {
                    ++j;
                    continue;
                }
                break;
            }
            if (ok && j < s.size() && s[j] == '>') {
                const std::string_view addr = s.substr(1, j - 1);
                const std::uint32_t l = append(inline_kind::link);
                node(l).destination = normalize_uri("mailto:" + std::string(addr));
                const std::uint32_t t = make(inline_kind::text, std::string(addr));
                append_child(l, t);
                m_pos += j + 1;
                return true;
            }
        }
        // URI: scheme (2-32 chars) ':' then no '<', '>', space or control
        std::size_t k = 1;
        if (k < s.size() && is_ascii_alpha(s[k])) {
            ++k;
            while (k < s.size() && k - 1 < 32 && (is_ascii_alnum(s[k]) || s[k] == '.' || s[k] == '+' || s[k] == '-')) ++k;
            if (k - 1 >= 2 && k < s.size() && s[k] == ':') {
                std::size_t e = k + 1;
                while (e < s.size() && s[e] != '<' && s[e] != '>' && static_cast<unsigned char>(s[e]) > 0x20) ++e;
                if (e < s.size() && s[e] == '>') {
                    const std::string_view uri = s.substr(1, e - 1);
                    const std::uint32_t l = append(inline_kind::link);
                    node(l).destination = normalize_uri(uri);
                    append_child(l, make(inline_kind::text, std::string(uri)));
                    m_pos += e + 1;
                    return true;
                }
            }
        }
        return false;
    }

    constexpr bool parse_html() {
        const std::size_t n = html_inline(m_s, m_pos, &m_html_misses);
        if (n == 0) return false;
        append(inline_kind::html, std::string(m_s.substr(m_pos, n)));
        m_pos += n;
        return true;
    }

    constexpr bool parse_entity() {
        auto m = match_entity(m_s, m_pos);
        if (!m) return false;
        text(m->text);
        m_pos += m->len;
        return true;
    }
};

using inline_parser = basic_inline_parser<>;

/// Appends the text content of an inline subtree: what a browser shows,
/// with breaks as '\n' (an image contributes its alt text, raw HTML
/// nothing). Iterative: any nesting depth is safe.
constexpr void append_plain(const inline_tree& t, std::uint32_t root, std::string& out) {
    std::uint32_t cur = t.nodes[root].first;
    bool entering = true;
    if (cur == none) return;
    while (cur != root) {
        const inline_node& n = t.nodes[cur];
        if (entering) {
            switch (n.type) {
                case inline_kind::text:
                case inline_kind::code: out += n.literal; break;
                case inline_kind::softbreak:
                case inline_kind::linebreak: out.push_back('\n'); break;
                default: break;
            }
            if (n.first != none) {
                cur = n.first;
                continue;
            }
        }
        if (n.next != none) {
            cur = n.next;
            entering = true;
        } else {
            cur = n.parent;
            entering = false;
        }
    }
}

}  // namespace pygim::pathlike::markdown
