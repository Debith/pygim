# -*- coding: utf-8 -*-
"""Tests for pathlike's markdown engine: ``pygim.path("x.md").read()`` and ``pathlike.markdown``.

The parser is held to the CommonMark 0.31.2 spec and the GFM extension
examples (vendored under tests/unittests/data/markdown), compared the way the
spec's own runner compares; the rest pins what the Document API promises:
lines and spans that slice the source, lossless edits, front matter through
the YAML and TOML engines, and builders whose output parses back.
"""

import ast
import copy
import datetime
import hashlib
import importlib.util
import os
import pickle
import re

import pytest

import pygim
from pygim import _stubs, pathlike

md = pathlike.markdown

DATA = pygim.path(__file__).parent / "data" / "markdown"


def _write(temp_dir, name, text):
    p = pygim.path(temp_dir) / name
    p.write_bytes(text.encode("utf-8"))   # exact bytes on every platform
    return p


def _normalizer():
    spec = importlib.util.spec_from_file_location("spec_normalize", DATA / "spec_normalize.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.normalize_html


normalize_html = _normalizer()


NOTES = """\
---
title: Notes
tags: [a, b]
---
# Notes

Intro with *emphasis* and `code`.

## Usage

Run `oo`:

```bash
oo enact read
```

## Usage

| Term | Meaning |
| :--- | ---: |
| a\\|b | **bold** |

- [x] done
- [ ] open

> quoted
> text

[ref]: https://example.com "Example"
"""


# --------------------------------------------------------------------------- #
# The engine: .md paths read into Documents and write them back
# --------------------------------------------------------------------------- #
def test_md_paths_are_markdown_paths():
    for name in ("notes.md", "NOTES.MD", "x.markdown"):
        p = pygim.path(name)
        assert isinstance(p, pathlike.mdpath)
        assert p.engine == "pygim-md"
    assert pygim.path("x.txt", engine="markdown").engine == "pygim-md"


def test_read_returns_a_document_over_the_exact_text(temp_dir):
    f = _write(temp_dir, "notes.md", NOTES)
    doc = pygim.path(f).read()
    assert isinstance(doc, md.Document)
    assert doc.text == NOTES and str(doc) == NOTES
    assert doc.dialect == "gfm" and doc.slugs == "github"


def test_write_round_trips_the_bytes(temp_dir):
    f = _write(temp_dir, "notes.md", NOTES)
    doc = pygim.path(f).read()
    out = pygim.path(temp_dir) / "copy.md"
    out.write(doc)
    with open(out, "rb") as written:   # read back without pygim: the check must not share what it checks
        assert written.read() == NOTES.encode("utf-8")
    out.write("# Plain text\n")
    assert out.read_bytes() == b"# Plain text\n"


def test_write_refuses_what_is_not_markdown(temp_dir):
    with pytest.raises(TypeError, match="markdown Document or str"):
        pygim.path(temp_dir / "x.md").write({"a": 1})


def test_a_parse_never_fails_but_invalid_utf8_does(temp_dir):
    f = pygim.path(temp_dir) / "bad.md"
    f.write_bytes(b"# ok\n\xff\xfe broken\n")
    with pytest.raises(RuntimeError, match="not valid UTF-8"):
        pygim.path(f).read()


# --------------------------------------------------------------------------- #
# Blocks: kinds, lines, spans that slice the source
# --------------------------------------------------------------------------- #
def names(blocks):
    return [type(b).__name__ for b in blocks]


def test_blocks_are_the_top_level_structure_each_of_its_own_class():
    doc = md.Document(NOTES)
    assert names(doc.blocks) == [
        "FrontMatter", "Heading", "Paragraph", "Heading", "Paragraph", "Code",
        "Heading", "Table", "List", "Quote", "Definition",
    ]
    assert all(isinstance(b, md.Block) for b in doc.walk())


def test_a_block_has_only_its_own_kind_s_properties():
    doc = md.Document(NOTES)
    (heading, paragraph) = doc.blocks[1:3]
    assert heading.level == 1 and not hasattr(paragraph, "level")
    assert not hasattr(heading, "rows") and not hasattr(heading, "lang")
    assert doc.find(md.Table)[0].align == ["left", "right"]


def test_find_takes_a_block_class():
    doc = md.Document(NOTES)
    assert names(doc.find(md.Heading)) == ["Heading"] * 3
    assert doc.find(md.Block) == doc.walk()
    with pytest.raises(TypeError, match="markdown's block classes"):
        doc.find("heading")


def test_every_block_slices_the_source_by_span_and_lines():
    doc = md.Document("é\n\n" + NOTES.split("---\n", 2)[2])   # a non-ASCII byte before everything
    lines = doc.text.splitlines(keepends=True)
    for b in doc.walk():
        start, end = b.span
        assert doc.text[start:end] == b.text
        first, last = b.lines
        assert b.text == "".join(lines[first - 1:last])   # nested blocks too: a span is whole lines


def test_walk_is_depth_first_and_children_know_their_parent():
    doc = md.Document("> - a\n>   - b\n")
    assert names(doc.walk()) == ["Quote", "List", "Item", "Paragraph", "List", "Item", "Paragraph"]
    inner = doc.find(md.Paragraph)[1]
    assert inner.plain == "b" and isinstance(inner.parent, md.Item) and isinstance(inner.parent.parent, md.List)
    assert doc.blocks[0].parent is None


def test_headings_have_level_title_and_unique_github_slugs():
    doc = md.Document(NOTES)
    hs = doc.find(md.Heading)
    assert [(h.level, h.title, h.slug) for h in hs] == [(1, "Notes", "notes"), (2, "Usage", "usage"), (2, "Usage", "usage-1")]


# Titles chosen to disagree with a careless port: non-ASCII, punctuation runs, repeats, explicit
# `_N` suffixes, numbers with leading zeros, titles that slug to nothing, Python's extra whitespace.
TOC_TITLES = ["Ünïcode  café", "Ünïcode  café", "C++ & C#", "a_1", "a", "a", "a_1", "x_007", "x_007", "x_9",
              "!!!", "!!!", "", "  spaced   out  ", "a\x1cb", "tab\tsep", "ﬁle ﬂow", "Straße", "日本語", "日本語",
              "emoji 🚀 here", "-dash-", "under_score", "MiXeD CaSe", "1. numbered", "a-b-c", "a - b - c"]


def test_toc_slugs_are_python_markdowns():
    toc = pytest.importorskip("markdown.extensions.toc")
    ids = set()
    expected = [toc.unique(toc.slugify(t, "-"), ids) for t in TOC_TITLES]
    doc = md.Document("".join(f"# {t}\n\n" for t in TOC_TITLES), slugs="toc")
    assert [h.slug for h in doc.find(md.Heading)] == expected


def test_github_slugs_keep_letters_and_drop_punctuation():
    github = md.Document("# Ünïcode  café\n\n# C++ & C#\n")
    assert [h.slug for h in github.find(md.Heading)] == ["ünïcode--café", "c--c"]


def test_hashes_inside_fenced_code_are_not_headings():
    doc = md.Document("# Real\n\n```md\n# Fake\n## 1. Fake\n```\n\n    # indented\n")
    assert [h.title for h in doc.find(md.Heading)] == ["Real"]
    assert [s.title for s in doc.sections] == ["Real"]


def test_setext_headings_are_headings():
    doc = md.Document("Title\n=====\n\nSub\n---\n")
    assert [(h.level, h.title) for h in doc.find(md.Heading)] == [(1, "Title"), (2, "Sub")]
    assert doc.find(md.Heading)[0].lines == (1, 2)


def test_code_blocks_give_language_info_and_code():
    doc = md.Document(NOTES)
    (code,) = doc.find(md.Code)
    assert (code.lang, code.info, code.code, code.fenced) == ("bash", "bash", "oo enact read\n", True)
    nested = md.Document("- item\n\n  ```py title=\"x\"\n  a = 1\n    b\n  ```\n").find(md.Code)[0]
    assert (nested.lang, nested.info, nested.code) == ("py", 'py title="x"', "a = 1\n  b\n")
    indented = md.Document("    x = 1\n").find(md.Code)[0]
    assert (indented.lang, indented.info, indented.code, indented.fenced) == (None, None, "x = 1\n", False)


def test_tables_give_header_rows_and_alignment_as_plain_text():
    (t,) = md.Document(NOTES).find(md.Table)
    assert t.header == ["Term", "Meaning"]
    assert t.rows == [["a|b", "bold"]]
    assert t.align == ["left", "right"]


def test_lists_and_task_items():
    doc = md.Document(NOTES)
    (lst,) = doc.find(md.List)
    assert (lst.ordered, lst.start, lst.tight) == (False, None, True)
    assert [i.checked for i in lst.children] == [True, False]
    assert [i.plain for i in lst.children] == ["done", "open"]
    ordered = md.Document("3. a\n\n4. b\n").find(md.List)[0]
    assert (ordered.ordered, ordered.start, ordered.tight) == (True, 3, False)
    assert md.Document("- a\n").find(md.Item)[0].checked is None


def test_quotes_contain_blocks():
    (q,) = md.Document(NOTES).find(md.Quote)
    assert names(q.children) == ["Paragraph"]
    assert q.plain == "quoted\ntext"


def test_definitions_are_blocks_and_resolve_references():
    doc = md.Document(NOTES + "\nSee [the site][ref].\n")
    (d,) = doc.find(md.Definition)
    assert (d.label, d.destination, d.title) == ("ref", "https://example.com", "Example")
    assert doc.find(md.Paragraph)[-1].plain == "See the site."
    assert '<a href="https://example.com" title="Example">the site</a>' in doc.html()


def test_plain_text_resolves_inline_markup():
    doc = md.Document("A *b* **c** `d` [e](/f) ![g](h.png) <span>i</span> &amp; \\*j\\*\n")
    assert doc.blocks[0].plain == "A b c d e g i & *j*"
    assert doc.blocks[0].content == "A *b* **c** `d` [e](/f) ![g](h.png) <span>i</span> &amp; \\*j\\*"
    assert md.Document(NOTES).plain.startswith("Notes\n\nIntro with emphasis and code.")


def test_crlf_line_endings_change_nothing_but_the_bytes(temp_dir):
    f = pygim.path(temp_dir) / "crlf.md"
    f.write_bytes(NOTES.replace("\n", "\r\n").encode("utf-8"))   # written as bytes: a text write would not test it
    crlf = pygim.path(f).read()
    lf = md.Document(NOTES)
    assert [(type(b), b.lines, b.plain) for b in crlf.walk()] == [(type(b), b.lines, b.plain) for b in lf.walk()]
    assert crlf.front_matter == lf.front_matter
    assert crlf.find(md.Code)[0].code == "oo enact read\n"
    for b in crlf.walk():
        assert crlf.text[b.span[0]:b.span[1]] == b.text


def test_stats_report_exact_bytes():
    doc = md.Document(NOTES)
    s = doc.stats()
    assert s["source"] == len(NOTES.encode("utf-8")) and s["blocks"] == len(doc.walk()) and s["lines"] == NOTES.count("\n")
    assert s["bytes"] > s["source"]
    bigger = md.Document(NOTES * 4).stats()
    assert bigger["bytes"] > s["bytes"]


def test_stats_bytes_count_each_buffer_once():
    # Two documents with the same tree that differ only in their text: the bytes differ by the text
    # (allocators round a capacity up a little, never by a second copy of anything).
    small, big = md.Document("x" * 10_000 + "\n"), md.Document("x" * 20_000 + "\n")
    assert abs((big.stats()["bytes"] - small.stats()["bytes"]) - 10_000) < 64


def test_repr_names_class_and_lines():
    doc = md.Document(NOTES)
    assert repr(doc.blocks[1]) == "Heading(lines 5-5, 'Notes')"
    assert repr(doc.sections[1]) == "Section(2, 'Usage', lines 9-16)"
    assert repr(doc).startswith("Document(gfm, 11 blocks, 29 lines")


# --------------------------------------------------------------------------- #
# Front matter: YAML and TOML through their engines
# --------------------------------------------------------------------------- #
def test_yaml_front_matter_reads_through_the_yaml_engine():
    doc = md.Document(NOTES)
    assert doc.front_matter == {"title": "Notes", "tags": ["a", "b"]}
    assert doc.blocks[0].lines == (1, 4) and doc.blocks[0].raw == "title: Notes\ntags: [a, b]"


def test_toml_front_matter_reads_through_the_toml_engine():
    doc = md.Document('+++\ntitle = "T"\ndate = 2026-10-02\n+++\n# T\n')

    assert doc.front_matter == {"title": "T", "date": datetime.date(2026, 10, 2)}


def test_no_front_matter_is_none_and_an_unclosed_fence_is_a_rule():
    assert md.Document("# x\n").front_matter is None
    doc = md.Document("---\nnot closed\n")
    assert doc.front_matter is None and isinstance(doc.blocks[0], md.ThematicBreak)
    assert isinstance(md.Document(NOTES, front_matter=False).blocks[0], md.ThematicBreak)


def test_invalid_front_matter_names_the_lines_and_the_file(temp_dir):
    f = _write(temp_dir, "bad.md", "---\nkey: [unclosed\n---\n# x\n")
    doc = pygim.path(f).read()
    assert doc.find(md.Heading)[0].title == "x"   # the body still reads
    with pytest.raises(RuntimeError, match=r"front matter \(lines 1-3\) of .*bad\.md"):
        doc.front_matter


def test_with_front_matter_replaces_adds_and_removes_it_and_keeps_the_body():
    doc = md.Document(NOTES)
    body = NOTES.split("---\n", 2)[2]
    changed = doc.with_front_matter({"title": "New"})
    assert changed.front_matter == {"title": "New"} and changed.text.endswith(body)
    assert md.Document(body).with_front_matter({"a": 1}).text == "---\na: 1\n---\n" + body
    assert doc.with_front_matter(None).text == body
    toml = md.Document(body).with_front_matter({"a": 1}, engine="toml")
    assert toml.text.startswith("+++\na = 1\n+++\n") and toml.front_matter == {"a": 1}


def test_only_a_fence_at_column_0_opens_or_closes_front_matter():
    # An indented `---` inside a YAML block scalar is the scalar's text, not the closing fence.
    doc = md.Document("---\nnotes: |\n  first part\n  ---\n  second part\ntitle: x\n---\n# Body\n")
    assert doc.front_matter == {"notes": "first part\n---\nsecond part\n", "title": "x"}
    assert [type(b).__name__ for b in doc.blocks] == ["FrontMatter", "Heading"]
    assert md.Document("  ---\na: 1\n---\n").front_matter is None   # an indented opening fence is body
    assert md.Document("---  \na: 1\n---\t\n").front_matter == {"a": 1}   # trailing spaces are fine


def test_front_matter_lines_end_as_the_bodys_do():
    doc = md.Document("---\rtitle: x\r---\r# h\r")   # lone CR, as CommonMark allows
    assert doc.front_matter == {"title": "x"}
    assert doc.find(md.Heading)[0].lines == (4, 4)


@pytest.mark.parametrize("data, engine", [
    pytest.param({"a": "x\n---\ny", "b": "..."}, "yaml", id="yaml fence-like value"),
    pytest.param({"a": "q\n+++\nz"}, "toml", id="toml fence-like value"),
])
def test_with_front_matter_reads_back_what_it_wrote(data, engine):
    doc = md.Document("# Body\n").with_front_matter(data, engine=engine)
    assert doc.front_matter == data
    assert [type(b).__name__ for b in doc.blocks] == ["FrontMatter", "Heading"] and doc.text.endswith("# Body\n")


def test_with_front_matter_writes_the_documents_line_ending():
    assert md.Document("# H\r\n").with_front_matter({"b": 2}).text == "---\r\nb: 2\r\n---\r\n# H\r\n"


def test_with_front_matter_needs_a_document_that_reads_front_matter():
    with pytest.raises(ValueError, match="front_matter=False"):
        md.Document("# H\n", front_matter=False).with_front_matter({"a": 1})


def test_a_front_matter_error_gives_the_files_line(temp_dir):
    body = "title: ok\nlist: [1, 2\nnext: x\n"
    yaml = _write(temp_dir, "alone.yaml", body.rstrip("\n"))   # the engine gets the body's lines, not its last line ending
    with pytest.raises(RuntimeError) as alone:
        pygim.path(yaml).read()
    md_file = _write(temp_dir, "in.md", "---\n" + body + "---\n# x\n")
    with pytest.raises(RuntimeError) as embedded:
        pygim.path(md_file).read().front_matter
    line = int(re.search(r"line (\d+)", str(alone.value)).group(1))
    assert f"line {line + 1}" in str(embedded.value) and "in.md" in str(embedded.value)   # one fence line above


def test_an_unknown_yaml_alias_names_the_front_matter_and_the_file(temp_dir):
    f = _write(temp_dir, "alias.md", "---\na: *nope\n---\n")
    with pytest.raises(RuntimeError, match=r"front matter \(lines 1-3\) of .*alias\.md"):
        pygim.path(f).read().front_matter


# --------------------------------------------------------------------------- #
# Sections and lossless edits
# --------------------------------------------------------------------------- #
def test_sections_nest_by_level_and_own_their_lines():
    doc = md.Document(NOTES)
    notes, usage, usage1 = doc.sections
    assert (notes.title, notes.level, notes.slug, notes.lines) == ("Notes", 1, "notes", (5, 29))
    assert (usage.lines, usage1.lines) == ((9, 16), (17, 29))
    assert notes.subsections == [usage, usage1] and usage.parent == notes and notes.parent is None
    assert names(usage.blocks) == ["Paragraph", "Code"]
    assert usage.text == doc.text[usage.span[0]:usage.span[1]] and usage.text.startswith("## Usage\n")
    assert doc.section("usage-1") == usage1 and doc.section("Notes") == notes


def test_section_lookup_names_what_exists():
    with pytest.raises(KeyError, match="no section 'missing'.*notes, usage, usage-1"):
        md.Document(NOTES).section("missing")


def test_replacing_a_section_changes_only_its_bytes():
    doc = md.Document(NOTES)
    usage = doc.sections[1]
    new = doc.replace(usage, "## Usage\n\nNothing to run.\n")
    start, end = usage.span
    assert new.text == NOTES[:start] + "## Usage\n\nNothing to run.\n\n" + NOTES[end:]   # the blank line before the next heading is kept
    assert [s.title for s in new.sections] == ["Notes", "Usage", "Usage"]


def test_replacing_a_block_keeps_its_separator_and_empty_text_deletes():
    doc = md.Document("a\n\nb\n\nc\n")
    assert doc.replace(doc.blocks[1], "B\n\n\n").text == "a\n\nB\n\nc\n"
    assert doc.replace(doc.blocks[1], "").text == "a\n\n\nc\n"
    assert doc.replace(doc.blocks[2], "C").text == "a\n\nb\n\nC\n"


def test_replace_refuses_a_nested_block_and_a_foreign_one():
    doc = md.Document("> a\n")
    with pytest.raises(ValueError, match="top-level.*a paragraph inside a quote"):
        doc.replace(doc.find(md.Paragraph)[0], "x")
    with pytest.raises(TypeError):
        doc.replace("not a block", "x")
    with pytest.raises(ValueError, match="another document"):
        doc.replace(md.Document("x\n").blocks[0], "y")


# --------------------------------------------------------------------------- #
# HTML: the spec's own examples, compared as its runner compares them
# --------------------------------------------------------------------------- #
COMMONMARK = (DATA / "commonmark-0.31.2.json").read()   # through pathlike's JSON engine
GFM = (DATA / "gfm-0.29-extensions.json").read()


def test_every_commonmark_example_renders_as_the_spec_says():
    failed = []
    for ex in COMMONMARK:
        got = md.Document(ex["markdown"], dialect="commonmark", front_matter=False).html()
        if normalize_html(got) != normalize_html(ex["html"]):
            failed.append(f"{ex['example']} ({ex['section']})")
    assert not failed, f"{len(failed)}/{len(COMMONMARK)} CommonMark 0.31.2 examples differ: {failed}"


def test_gfm_adds_tables_strikethrough_and_task_items_and_breaks_no_commonmark_example():
    failed = [f"{ex['example']} ({ex['extension']})" for ex in GFM
              if normalize_html(md.Document(ex["markdown"], front_matter=False).html()) != normalize_html(ex["html"])]
    failed += [str(ex["example"]) for ex in COMMONMARK
               if normalize_html(md.Document(ex["markdown"], front_matter=False).html()) != normalize_html(ex["html"])]
    assert not failed, failed


def test_block_html_renders_one_block():
    doc = md.Document("# T\n\n> *q*\n")
    assert doc.blocks[1].html() == "<blockquote>\n<p><em>q</em></p>\n</blockquote>\n"


# --------------------------------------------------------------------------- #
# Writing markdown: builders whose output parses back
# --------------------------------------------------------------------------- #
TRICKY = ["a*b*c", "[x](y)", "# not a heading", "1. not a list", "- not a list", "> not a quote", "a | b",
          "`code`", "&amp; stays", "<b>bold</b>", "_under_", "~~strike~~", "back\\slash", "100% + 1 = 2",
          "=== not setext", "***", "!", "C#", "x # y #", "Ünïcode ✓"]


@pytest.mark.parametrize("text", TRICKY)
def test_escaped_text_reads_back_as_itself(text):
    assert md.Document(md.escape(text)).blocks[0].plain == text
    assert md.Document(md.heading(2, md.escape(text))).find(md.Heading)[0].title == text


def test_heading_validates_its_level():
    assert md.heading(3, "T") == "### T\n"
    with pytest.raises(ValueError, match="level must be 1-6, got 7"):
        md.heading(7, "T")


def test_code_picks_a_fence_its_code_cannot_close():
    text = "```\nnot the end\n````\n"
    block = md.code(text, lang="md")
    assert block.startswith("`````md\n")
    (c,) = md.Document(block).find(md.Code)
    assert (c.code, c.lang) == (text, "md")
    assert md.Document(md.code("x", lang="weird`lang")).find(md.Code)[0].info == "weird`lang"


def test_table_round_trips_its_cells_and_lines_up():
    rows = [["a|b", 1], ["multi\nline", "`x|y`"], ["short"]]
    text = md.table(["Key", "Value"], rows, align=["left", "right"])
    assert text.splitlines()[:2] == ["| Key           |  Value |", "| :------------ | -----: |"]   # widest cell: multi<br>line
    (t,) = md.Document(text).find(md.Table)
    assert t.header == ["Key", "Value"] and t.align == ["left", "right"]
    assert t.rows == [["a|b", "1"], ["multiline", "x|y"], ["short", ""]]
    with pytest.raises(ValueError, match="align"):
        md.table(["a"], [], align=["middle"])


def test_bullets_quote_and_join_build_documents():
    text = md.join([md.heading(1, "T"), md.bullets(["one", "two\nlines"]), md.bullets(["a", "b"], numbered=True, start=3),
                    md.quote("q\n\nr"), ""])
    doc = md.Document(text)
    assert names(doc.blocks) == ["Heading", "List", "List", "Quote"]
    assert [i.plain for i in doc.blocks[1].children] == ["one", "two\nlines"]
    assert doc.blocks[2].start == 3 and doc.blocks[3].plain == "q\n\nr"
    assert text.endswith("> r\n") and "\n\n\n" not in text


def test_front_matter_builder_uses_the_engines():
    assert md.front_matter({"a": 1}) == "---\na: 1\n---\n"
    assert md.front_matter({"a": 1}, engine="toml") == "+++\na = 1\n+++\n"
    with pytest.raises(ValueError, match="yaml, toml"):
        md.front_matter({"a": 1}, engine="json")


# --------------------------------------------------------------------------- #
# What the review found no test for
# --------------------------------------------------------------------------- #
def test_every_kind_property_reads():
    doc = md.Document("+++\na = 1\n+++\n# Title *x*\n\n<div>\nraw\n</div>\n\n"
                      "| l | c | r | n |\n|:--|:-:|--:|---|\n| 1 | 2 | 3 | 4 |\n")
    assert doc.blocks[0].engine == "toml" and doc.blocks[0].raw == "a = 1"
    heading = doc.find(md.Heading)[0]
    assert heading.content == "Title *x*" and heading.title == "Title x"
    assert doc.find(md.Html)[0].raw == "<div>\nraw\n</div>"
    assert doc.find(md.Table)[0].align == ["left", "center", "right", None]
    assert doc.sections[0].heading == heading
    assert md.Document("---\na: 1\n---\n").blocks[0].engine == "yaml"


def test_a_toml_front_matter_error_names_the_file_and_its_line(temp_dir):
    f = _write(temp_dir, "bad.md", "+++\nok = 1\nbad = = 2\n+++\n# x\n")
    with pytest.raises(RuntimeError, match=r"TOML parse error \(front matter \(lines 1-4\) of .*bad\.md, line 3\)"):
        pygim.path(f).read().front_matter


def test_with_front_matter_keeps_the_format_already_there():
    doc = md.Document("+++\na = 1\n+++\n# x\n").with_front_matter({"a": 2})
    assert doc.text.startswith("+++\na = 2\n+++\n") and doc.front_matter == {"a": 2}


@pytest.mark.parametrize("kw, choices", [
    ({"dialect": "rst"}, "markdown dialect must be one of gfm, commonmark, got 'rst'"),
    ({"slugs": "pandoc"}, "markdown slugs must be one of github, toc, got 'pandoc'"),
])
def test_a_policy_that_does_not_exist_names_the_ones_that_do(kw, choices):
    with pytest.raises(ValueError, match=re.escape(choices)):
        md.Document("# x\n", **kw)


def test_a_pinned_md_engine_reads_any_extension(temp_dir):
    f = _write(temp_dir, "notes.txt", NOTES)
    doc = pathlike.mdpath(f).read()
    assert isinstance(doc, md.Document) and doc.text == NOTES


# --------------------------------------------------------------------------- #
# Answers the review found wrong, each against its reference
# --------------------------------------------------------------------------- #
# escape(): whitespace a paragraph would strip or read as structure, and a lone
# CR, are written as character references, so the text comes back exactly.
EDGE_TEXT = [" > q", "  - x", "   1. x", "    code", "\tx", "\t- x", "a\n ===", "a\n  ---", "a\n:-:",
             "a\r- b", "a\r\nb", "trailing  ", "a  \nb", "  ", "x\n\ty"]


@pytest.mark.parametrize("text", EDGE_TEXT, ids=repr)
def test_escape_round_trips_whitespace_and_line_starts(text):
    doc = md.Document(md.escape(text))
    assert [type(b).__name__ for b in doc.blocks] == ["Paragraph"] and doc.blocks[0].plain == text
    if "\n" not in text and "\r" not in text:
        assert md.Document(md.heading(2, md.escape(text))).find(md.Heading)[0].title == text


def test_quote_and_bullets_treat_every_line_ending_as_one():
    assert [type(b).__name__ for b in md.Document(md.quote("a\r# b")).blocks] == ["Quote"]
    assert [type(b).__name__ for b in md.Document(md.bullets(["a\r# b"])).blocks] == ["List"]
    assert md.table(["h"], [["a\rb"]]) == md.table(["h"], [["a\nb"]]) == md.table(["h"], [["a\r\nb"]])   # a <br>


def test_indented_code_ends_at_its_last_line_of_code():
    assert md.Document("    code\n\n\npara\n").blocks[0].lines == (1, 1)   # blank lines belong to no block
    # so the blank line after it separates list items, and the list is loose (CommonMark 5.3)
    assert md.Document("-     code\n\n- b\n").html() == (
        "<ul>\n<li>\n<pre><code>code\n</code></pre>\n</li>\n<li>\n<p>b</p>\n</li>\n</ul>\n")


@pytest.mark.parametrize("title, slug", [
    ("Λόγος", "λόγος"), ("ΛΌΓΟΣ ΚΑΙ ΣΑΣ", "λόγος-και-σας"),   # lower-case with the final sigma, as JS does
    ("µs", "µs"), ("ſtraße", "ſtraße"), ("ꭰ", "ꭰ"),            # lower-casing, not case folding
    ("İstanbul", "i̇stanbul"),                                   # İ lower-cases to i + U+0307
])
def test_github_slugs_lower_case_as_github_does(title, slug):
    assert md.Document(f"# {title}\n").find(md.Heading)[0].slug == slug
    assert slug == title.lower().replace(" ", "-")   # Python's str.lower is the same mapping as JS toLowerCase here


def test_a_document_is_text_not_bytes():
    with pytest.raises(TypeError, match="str"):
        md.Document(b"# a\n")


@pytest.mark.parametrize("call", [
    pytest.param(lambda: md.Document("# a\ud800\n"), id="Document"),
    pytest.param(lambda: (lambda d: d.replace(d.blocks[0], "x\ud800"))(md.Document("# a\n")), id="replace"),
    pytest.param(lambda: md.heading(1, "\ud800"), id="heading"),
    pytest.param(lambda: md.escape("\ud800"), id="escape"),
    pytest.param(lambda: md.bullets(["\ud800"]), id="bullets"),
])
def test_text_that_is_not_utf8_is_a_value_error_naming_it(call):
    with pytest.raises(ValueError, match="UTF-8"):
        call()


def test_writing_a_lone_surrogate_names_the_engine(temp_dir):
    with pytest.raises(ValueError, match="md write.*UTF-8"):
        pygim.path(temp_dir / "s.md").write("# a\ud800\n")


def test_nul_reaches_no_output():
    assert md.Document("<div>\x00</div>\n").html() == "<div>\ufffd</div>\n"   # raw HTML block
    assert md.Document('a <span title="\x00">x</span>\n').html() == '<p>a <span title="\ufffd">x</span></p>\n'
    assert md.Document("a\x00b\n").plain == "a\ufffdb"


def test_a_backslash_before_a_pipe_escapes_it_in_a_table_cell():
    table = md.Document("| a | b |\n|---|---|\n| x \\\\| y | z |\n").find(md.Table)[0]
    assert table.rows == [["x | y", "z"]]   # cmark-gfm: a pipe after a backslash never splits a cell


@pytest.mark.parametrize("start, n", [(-1, 1), (999_999_999, 2), (4_294_967_295, 1), (1_000_000_000, 1)])
def test_an_ordered_list_numbers_within_nine_digits(start, n):
    with pytest.raises(ValueError, match="9 digits"):
        md.bullets(["x"] * n, numbered=True, start=start)
    assert md.bullets(["a", "b"], numbered=True, start=999_999_998).startswith("999999998. a")


def test_a_table_refuses_cells_its_header_cannot_hold():
    with pytest.raises(ValueError, match="row 1 has 3 cells"):
        md.table(["a", "b"], [["1", "2", "3"]])
    with pytest.raises(ValueError, match="align"):
        md.table(["a", "b"], [["1", "2"]], align=["left"])


def test_an_absent_definition_title_is_none():
    d = md.Document("[x]: /u\n").find(md.Definition)[0]
    assert d.title is None and d.destination == "/u"


def test_find_names_the_classes_it_takes():
    class Mine(md.Heading):
        pass

    with pytest.raises(TypeError, match="markdown's block classes"):
        md.Document("# a\n").find(Mine)


def test_a_document_copies_and_pickles_as_its_text_and_policies():

    doc = md.Document("+++\na = 1\n+++\n# A\n", dialect="commonmark", slugs="toc")
    assert copy.copy(doc) is doc and copy.deepcopy(doc) is doc   # immutable: a copy is itself
    back = pickle.loads(pickle.dumps(doc))
    assert (back.text, back.dialect, back.slugs, back.front_matter) == (doc.text, "commonmark", "toc", {"a": 1})
    assert pickle.loads(pickle.dumps(md.Document("---\n", front_matter=False))).blocks[0].__class__ is md.ThematicBreak


def test_engine_writes_refuse_a_wrong_kind_with_a_type_error_naming_the_engine():
    with pytest.raises(TypeError, match="yaml write: .*set"):
        md.front_matter({"a": {1, 2}})
    with pytest.raises(TypeError, match="toml write: .*mapping"):
        md.front_matter([1], engine="toml")


# --------------------------------------------------------------------------- #
# The module's surface; the SIMD stop scan at every byte offset; generated tables
# --------------------------------------------------------------------------- #
PUBLIC = {"Block", "Code", "Definition", "Document", "FrontMatter", "Heading", "Html", "Item", "List", "Paragraph",
          "Quote", "Section", "Table", "ThematicBreak",
          "bullets", "code", "escape", "front_matter", "heading", "join", "quote", "table"}


def test_the_module_exports_its_api_and_no_test_hooks():
    # Only a build made with PYGIM_MARKDOWN_PROBES=1 binds the probes; any other name is a leak.
    probes = {"_stops", "_scan"} if os.environ.get("PYGIM_MARKDOWN_PROBES") == "1" else set()
    assert {n for n in dir(md) if not n.startswith("__")} == PUBLIC | probes


def _stub_markdown():
    """The stub's `class markdown:` as {class: {name: kind}} and {function: [parameters]}."""
    tree = ast.parse(_stubs.stub_path().read_text(encoding="utf-8"))
    ns = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "markdown")

    def kind(f):
        return "property" if any(getattr(d, "id", None) == "property" for d in f.decorator_list) else "method"

    def params(f):
        a = f.args
        names = [x.arg for x in a.posonlyargs + a.args if x.arg != "self"]
        return names + (["*"] + [x.arg for x in a.kwonlyargs] if a.kwonlyargs else [])

    classes = {c.name: {f.name: kind(f) for f in c.body if isinstance(f, ast.FunctionDef)}
               for c in ns.body if isinstance(c, ast.ClassDef)}
    functions = {f.name: params(f) for f in ns.body if isinstance(f, ast.FunctionDef)}
    methods = {f.name: params(f) for c in ns.body if isinstance(c, ast.ClassDef) and c.name == "Document"
               for f in c.body if isinstance(f, ast.FunctionDef) and kind(f) == "method"}
    return classes, functions, methods


def _runtime_params(fn):
    """Parameter names from pybind11's signature line: `bullets(items: object, *, numbered: bool = False, ...)`."""
    head = fn.__doc__.splitlines()[0]
    inside = head[head.index("(") + 1:head.rindex(")")]
    out = []
    for part in [p.strip() for p in inside.split(",") if p.strip()]:
        name = part.split(":")[0].split("=")[0].strip()
        if name != "self":
            out.append(name)
    return out


def test_the_stub_matches_the_module():
    classes, functions, methods = _stub_markdown()
    for name, members in classes.items():
        cls = getattr(md, name)
        runtime = {n: "property" if isinstance(v, property) else "method"
                   for n, v in vars(cls).items() if not n.startswith("_")}
        stubbed = {n: k for n, k in members.items() if not n.startswith("_")}
        assert runtime == stubbed, f"markdown.{name}: runtime {runtime} != stub {stubbed}"
        for dunder in (n for n in members if n.startswith("__")):
            assert dunder in vars(cls), f"markdown.{name}.{dunder} is in the stub but not bound"
    assert set(classes) == {n for n in dir(md) if not n.startswith("_") and isinstance(getattr(md, n), type)}
    assert set(functions) == {n for n in dir(md) if not n.startswith("_") and not isinstance(getattr(md, n), type)}
    for name, stub_params in functions.items():
        assert _runtime_params(getattr(md, name)) == stub_params, f"markdown.{name}()"
    for name, stub_params in methods.items():
        if not name.startswith("__"):
            assert _runtime_params(getattr(md.Document, name)) == stub_params, f"Document.{name}()"
    assert _runtime_params(md.Document.__init__) == ["text", "*", "dialect", "slugs", "front_matter"]


# Each stop byte, at every offset across two 64-byte words, must change the parse
# as markup does: a stop the SIMD scan missed would leave the markup as text.
STOPS = {
    "*": ("*a*", "a"), "_": ("-_a_", "-a"), "`": ("`a`", "a"), "[": ("[a](u)", "a"), "!": ("![a](u)", "a"),
    "<": ("<b>a</b>", "a"), "&": ("&amp;", "&"), "\\": ("\\*", "*"), "~": ("~~a~~", "a"),
}


@pytest.mark.parametrize("stop", sorted(STOPS))
def test_the_stop_scan_finds_every_stop_at_every_offset(stop):
    markup, text = STOPS[stop]
    for n in range(140):
        assert md.Document("x" * n + markup).blocks[0].plain == "x" * n + text, (stop, n)
    for n in range(1, 140):   # a line break: two trailing spaces make a hard break only if the newline is seen
        assert "<br />" in md.Document("x" * n + "  \nb").html(), n


def test_generated_unicode_tables_are_current():
    """tables.h records the generator's digest and its own body's: a stale or
    hand-edited table fails here without regenerating 1.1 million code points."""

    root = pygim.path(__file__).parents[2]
    gen = root / "tests" / "static" / "gen_markdown_tables.py"
    header = root / "src" / "_pygim_fast" / "pathlike" / "markdown" / "tables.h"
    if not header.is_file():
        pytest.skip("source tree not present (installed wheel)")
    text = header.read_bytes().replace(b"\r\n", b"\n").decode("utf-8")   # an autocrlf checkout holds CRLF (#31)
    head, body = text.split("\n\n", 1)
    generator = hashlib.sha256(gen.read_bytes().replace(b"\r\n", b"\n")).hexdigest()   # CRLF read as LF (#31)
    assert f"generator sha256 {generator}" in head, \
        "stale: tests/static/gen_markdown_tables.py changed; run it"
    assert f"body sha256 {hashlib.sha256(body.encode('utf-8')).hexdigest()}" in head, \
        "tables.h was edited by hand; run tests/static/gen_markdown_tables.py"
