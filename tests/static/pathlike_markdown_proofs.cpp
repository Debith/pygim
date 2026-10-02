// Compile-time proofs for pathlike/markdown/ — the pybind-free markdown core.
//
// Part of the pygim.pathlike extension's SOURCES (ext.pathlike.toml), so every
// build proves these laws and a violated one produces no binary. It adds no
// run-time code. The run-time suite (tests/unittests/test_pathlike_markdown.py)
// holds the whole parser to the CommonMark and GFM spec examples; what is
// proven here are the rules everything else rests on — each line classifier,
// the link grammars, entities, both slug policies, the scalar stop scan, the
// writer's structural guarantees, and whole parses under both dialects.
//
// Discipline (as pathlike_core_proofs.cpp): every proof is a consteval
// function returning a bool computed on local objects, so no std::string
// escapes into an assertion (GCC 13 and 14 cannot constant-evaluate that).

#include "../../src/_pygim_fast/pathlike/markdown/document.h"
#include "../../src/_pygim_fast/pathlike/markdown/writer.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pygim::pathlike::markdown;

// ── line classifiers ───────────────────────────────────────────────────────
static_assert(atx_level("# x") == 1 && atx_level("###### x") == 6 && atx_level("#") == 1 && atx_level("#\tx") == 1);
static_assert(atx_level("####### x") == 0 && atx_level("#x") == 0 && atx_level("x #") == 0 && atx_level("") == 0);
static_assert(atx_content(" Title ##  ") == "Title" && atx_content(" C#") == "C#" && atx_content(" a \\#") == "a \\#");
static_assert(atx_content(" ### ") == "" && atx_content(" #5 ") == "#5" && atx_content("x #b") == "x #b");
static_assert(setext_level("===") == 1 && setext_level("-- \t") == 2 && setext_level("=") == 1);
static_assert(setext_level("= =") == 0 && setext_level("-=") == 0 && setext_level("") == 0);
static_assert(is_thematic_break("***") && is_thematic_break("- - -") && is_thematic_break("_\t_ _  "));
static_assert(!is_thematic_break("**") && !is_thematic_break("*-*") && !is_thematic_break("--- x"));
static_assert(fence_open("```py").len == 3 && fence_open("~~~~").ch == '~' && fence_open("````x").len == 4);
static_assert(fence_open("``").len == 0 && fence_open("``` a`b").len == 0 && fence_open("~~~ a`b").len == 3);
static_assert(fence_closes("````", '`', 3) && fence_closes("```  ", '`', 3) && !fence_closes("``", '`', 2));
static_assert(!fence_closes("```", '`', 4) && !fence_closes("``` x", '`', 3) && !fence_closes("~~~", '`', 3));

static_assert(html_block_start("<script>") == 1 && html_block_start("<PRE class=x>") == 1 && html_block_start("<style") == 1);
static_assert(html_block_start("<!-- c") == 2 && html_block_start("<?php") == 3 && html_block_start("<!DOCTYPE html>") == 4);
static_assert(html_block_start("<![CDATA[") == 5 && html_block_start("<div>") == 6 && html_block_start("</TABLE x") == 6);
static_assert(html_block_start("<a href=\"x\">") == 7 && html_block_start("</custom-tag>  ") == 7);
static_assert(html_block_start("<a> text") == 0 && html_block_start("<divx>") == 7 && html_block_start("< div>") == 0);
static_assert(html_block_ends(1, "x</STYLE>y") && html_block_ends(2, "-->") && html_block_ends(3, "?>") &&
              html_block_ends(4, ">") && html_block_ends(5, "]]>") && !html_block_ends(2, "--") && !html_block_ends(6, "x"));

consteval bool delimiter_rows() {
    std::vector<align> a;
    const bool row = table_delimiter_row("| :-- | :-: | --: | --- |", a) && a.size() == 4 && a[0] == align::left &&
                     a[1] == align::center && a[2] == align::right && a[3] == align::none;
    const bool bare = table_delimiter_row("-|-", a) && a.size() == 2;
    const bool rejects = !table_delimiter_row("| --- | x |", a) && !table_delimiter_row("|", a) && !table_delimiter_row("| : |", a);
    return row && bare && rejects;
}
static_assert(delimiter_rows());

consteval bool table_rows() {
    std::vector<std::pair<std::size_t, std::size_t>> c;
    const std::string_view s = "| a | b\\|c |  ";
    table_cells(s, c);
    const bool escaped_pipe = c.size() == 2 && s.substr(c[0].first, c[0].second - c[0].first) == "a" &&
                              s.substr(c[1].first, c[1].second - c[1].first) == "b\\|c";
    table_cells("x | y", c);
    const bool no_outer_pipes = c.size() == 2;
    table_cells("|", c);
    const bool one_empty = c.size() == 1 && c[0].first == c[0].second;
    return escaped_pipe && no_outer_pipes && one_empty;
}
static_assert(table_rows());

// ── link grammars, entities, URIs ──────────────────────────────────────────
static_assert(link_label("[foo] x", 0) == 5 && link_label("[a\\]b]", 0) == 6 && link_label("[a[b]", 0) == 0 && link_label("[a", 0) == 0);
static_assert(is_punctuation(U'!') && is_punctuation(U'\u00A7') && is_punctuation(U'\u2014') && !is_punctuation(U'a') && !is_punctuation(U'\u00E9'));
static_assert(is_whitespace(U' ') && is_whitespace(U'\u00A0') && is_whitespace(U'\u3000') && !is_whitespace(U'x'));
static_assert(entity("amp") == "&" && entity("ouml") == "\xC3\xB6" && entity("bogus").empty());

consteval bool references_and_entities() {
    const auto amp = match_entity("&amp;x", 0);
    const auto dec = match_entity("&#65;", 0);
    const auto hex = match_entity("&#x1F600;", 0);
    const auto zero = match_entity("&#0;", 0);
    const bool entities = amp && amp->text == "&" && amp->len == 5 && dec && dec->text == "A" && hex &&
                          hex->text == "\xF0\x9F\x98\x80" && zero && zero->text == "\xEF\xBF\xBD" &&
                          !match_entity("&bogus;", 0) && !match_entity("&#;", 0) && !match_entity("&amp", 0);
    const bool unescaped = unescape("\\*a\\* &amp; &#65; \\q") == "*a* & A \\q";
    const bool uris = normalize_uri("/a b?\xC3\xB6") == "/a%20b?%C3%B6" && normalize_uri("%41%zz") == "%41%25zz" &&
                      escape_html("<a & \"b\">") == "&lt;a &amp; &quot;b&quot;&gt;";
    const bool labels = fold_label(" Foo\n  BAR ") == "foo bar" && fold_label("\xE1\xBA\x9E") == "ss";   // U+1E9E folds to "ss"
    return entities && unescaped && uris && labels;
}
static_assert(references_and_entities());

consteval bool link_parts() {
    const auto angle = link_destination("</my url>)", 0);
    const auto bare = link_destination("a(b)c d", 0);
    const auto empty = link_destination(")", 0);
    const auto title = link_title("\"a \\\"b\\\"\" x", 0);
    const bool dest = angle && angle->value == "/my%20url" && angle->len == 9 && bare && bare->value == "a(b)c" &&
                      empty && empty->value.empty() && !link_destination("(", 0) && !link_destination("<a\nb>", 0);
    const bool deep = !link_destination(std::string(max_paren_depth + 1, '('), 0);   // the nesting bound
    const bool titled = title && title->value == "a \"b\"" && !link_title("(a(b)", 0);
    const auto ref = link_reference("[Foo]: /url \"T\"\nrest");
    const auto no_title = link_reference("[a]: /u\n\"t\" x");
    const bool refs = ref && ref->label == "foo" && ref->destination == "/url" && ref->title == "T" && ref->len == 16 &&
                      no_title && no_title->title.empty() && no_title->len == 8 && !link_reference("[a]: /u x") &&
                      !link_reference("[ ]: /u");
    return dest && deep && titled && refs;
}
static_assert(link_parts());

consteval bool raw_html() {
    return html_inline("<a href='x'>", 0) == 12 && html_inline("<br/>", 0) == 5 && html_inline("</a >", 0) == 5 &&
           html_inline("<!-- c -->", 0) == 10 && html_inline("<!-->", 0) == 5 && html_inline("<?x?>", 0) == 5 &&
           html_inline("<!X y>", 0) == 6 && html_inline("<![CDATA[x]]>", 0) == 13 && html_inline("<a b=>", 0) == 0 &&
           html_inline("<1>", 0) == 0;
}
static_assert(raw_html());

// ── slugs: both policies ───────────────────────────────────────────────────
template <SlugPolicy S>
consteval bool slug_is(std::string_view text, std::string_view expected) {
    return slug<S>(text) == expected;
}
static_assert(slug_is<github_slug>("Hello, World!", "hello-world") && slug_is<toc_slug>("Hello, World!", "hello-world"));
static_assert(slug_is<github_slug>("\xC3\x9Cn\xC3\xAF  caf\xC3\xA9", "\xC3\xBCn\xC3\xAF--caf\xC3\xA9") &&
              slug_is<toc_slug>("\xC3\x9Cn\xC3\xAF  caf\xC3\xA9", "uni-cafe"));
static_assert(slug_is<github_slug>("C++ & C#", "c--c") && slug_is<toc_slug>("C++ & C#", "c-c"));
static_assert(slug_is<github_slug>("2.1 snake_case", "21-snake_case") && slug_is<toc_slug>("2.1 snake_case", "21-snake_case"));
static_assert(slug_is<github_slug>("Stra\xC3\x9F" "e", "stra\xC3\x9F" "e") && slug_is<toc_slug>("\xEF\xAC\x81le", "file"));   // ß stays; ﬁ folds

// ── the scalar stop scan (the SIMD policies are fuzzed against it at run time) ──
consteval bool stops() {
    const basic_stop_index<scalar_scan> ix("a *b* `c`");
    const basic_stop_index<scalar_scan> empty("");
    const basic_stop_index<scalar_scan> far(std::string(130, 'x') + "_");
    return ix.next(0) == 2 && ix.next(3) == 4 && ix.next(5) == 6 && ix.next(9) == ix.npos && empty.next(0) == empty.npos &&
           far.next(0) == 130 && far.next(131) == far.npos;
}
static_assert(stops());

// ── the writer: its output is the block it means ───────────────────────────
consteval bool writer() {
    const bool esc = write::escape("a*b* [c] # 1. x") == "a\\*b\\* \\[c\\] \\# 1. x" && write::escape("1. x\n- y") == "1\\. x\n\\- y" &&
                     write::escape("&amp; & x") == "\\&amp; & x";
    const bool fences = write::code("x = `1`\n", "py") == "```py\nx = `1`\n```\n" &&
                        write::code("```` x", "") == "`````\n```` x\n`````\n" && write::code("x", "a`b") == "~~~a`b\nx\n~~~\n";
    const bool heads = write::heading(2, "Usage\nmore") == "## Usage more\n";
    const bool grid = write::table({"k", "v"}, {{"a|b", "1"}}, {}) == "| k    | v   |\n| ---- | --- |\n| a\\|b | 1   |\n" &&
                      write::table({"x"}, {}, {align::center}) == "|  x  |\n| :-: |\n";
    const bool lists = write::items({"one", "two\nlines"}, false, 1) == "- one\n- two\n  lines\n" &&
                       write::items({"a", "b"}, true, 9) == "9. a\n10. b\n";
    const bool quotes = write::quote("a\n\nb") == "> a\n>\n> b\n" && write::join({"# T\n", "", "text\n\n"}) == "# T\n\ntext\n";
    const bool matter = write::front_matter("a: 1", '-') == "---\na: 1\n---\n" && write::front_matter("a = 1\n", '+') == "+++\na = 1\n+++\n";
    return esc && fences && heads && grid && lists && quotes && matter;
}
static_assert(writer());

// ── whole parses, under both dialects ──────────────────────────────────────
template <Dialect D>
consteval bool quote_parse() {
    const tree t = basic_block_parser<D>{}.parse("# T\n\n> a\n> b\n");
    const block& doc = t.blocks[0];
    const block& h = t.blocks[doc.first];
    const block& q = t.blocks[h.next];
    const block& p = t.blocks[q.first];
    return doc.last_line == 4 && h.type == kind::heading && h.level == 1 && h.first_line == 1 && h.end == 4 &&
           q.type == kind::quote && q.first_line == 3 && q.last_line == 4 && q.begin == 5 && q.end == 13 &&
           p.type == kind::paragraph && p.nseg == 2 && q.next == none;
}
static_assert(quote_parse<gfm>() && quote_parse<commonmark>());

template <Dialect D>
consteval kind first_kind(std::string_view src) {
    const tree t = basic_block_parser<D>{}.parse(src);
    return t.blocks[t.blocks[0].first].type;
}
static_assert(first_kind<gfm>("a | b\n--|--\n") == kind::table && first_kind<commonmark>("a | b\n--|--\n") == kind::paragraph);
static_assert(first_kind<gfm>("```\n# not a heading\n```\n") == kind::code && first_kind<gfm>("---\nx: 1\n---\n") == kind::front_matter);
static_assert(first_kind<gfm>("    code\n") == kind::code && first_kind<gfm>("<div>\n") == kind::html && first_kind<gfm>("[a]: /u\n") == kind::definition);

consteval bool front_matter_and_tasks() {
    const tree fm = basic_block_parser<gfm>{}.parse("+++\na = 1\n+++\n# T\n");
    const block& f = fm.blocks[fm.blocks[0].first];
    const tree off = basic_block_parser<gfm>{}.parse("---\nx\n---\n", false);
    const tree tasks = basic_block_parser<gfm>{}.parse("- [x] a\n- [ ] b\n- c\n");
    const block& list = tasks.blocks[tasks.blocks[0].first];
    const block& i1 = tasks.blocks[list.first];
    const block& i2 = tasks.blocks[i1.next];
    const block& i3 = tasks.blocks[i2.next];
    const tree cm = basic_block_parser<commonmark>{}.parse("- [x] a\n");
    const block& cm_item = cm.blocks[cm.blocks[cm.blocks[0].first].first];
    return f.type == kind::front_matter && f.marker == '+' && f.last_line == 3 && f.nseg == 1 &&
           off.blocks[off.blocks[0].first].type == kind::thematic_break && i1.task == 1 && i2.task == 0 && i3.task == -1 &&
           list.tight && cm_item.task == -1;
}
static_assert(front_matter_and_tasks());

consteval bool definitions_resolve() {
    const tree t = basic_block_parser<gfm>{}.parse("[Foo]: /u1\n[foo]: /u2\n\n[FOO]\n");
    const definition* d = t.lookup("foo");
    return t.definitions.size() == 2 && d && d->destination == "/u1" && !t.lookup("bar");   // the first of a label wins
}
static_assert(definitions_resolve());

template <Dialect D>
consteval bool inline_plain(std::string_view src, std::string_view expected) {
    const tree t = basic_block_parser<D>{}.parse(src);
    const inline_tree it = basic_inline_parser<D, scalar_scan>{}.parse(leaf_text(src, t, t.blocks[t.blocks[0].first]), t);
    std::string out;
    append_plain(it, 0, out);
    return out == expected;
}
static_assert(inline_plain<gfm>("*a* `b` [c](/d) ~~e~~ \\*f\n", "a b c e *f"));
static_assert(inline_plain<commonmark>("~~e~~ snake_case_name\n", "~~e~~ snake_case_name"));

consteval bool rendered() {
    const std::string src = "# T\n\n- *a*\n- [b](/u \"t\")\n";
    const tree t = basic_block_parser<gfm>{}.parse(src);
    return basic_html_renderer<gfm, scalar_scan>{}.render(src, t) ==
           "<h1>T</h1>\n<ul>\n<li><em>a</em></li>\n<li><a href=\"/u\" title=\"t\">b</a></li>\n</ul>\n";
}
static_assert(rendered());

consteval bool document_sections() {
    const basic_document<gfm, github_slug, scalar_scan> d("# A\n\nx\n\n## B\n\ny\n\n## B\n");
    const auto& s = d.sections();
    const auto& h = d.headings();
    return s.size() == 3 && s[0].first_line == 1 && s[0].last_line == 9 && s[1].first_line == 5 && s[1].last_line == 8 &&
           s[1].parent == 0 && h[1].slug == "b" && h[2].slug == "b-1" &&
           d.splice(s[1].begin, s[1].end, "## C\n") == "# A\n\nx\n\n## C\n\n## B\n";
}
static_assert(document_sections());

}  // namespace
