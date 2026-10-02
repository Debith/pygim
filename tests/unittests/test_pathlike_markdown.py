# -*- coding: utf-8 -*-
"""Tests for pathlike's markdown engine: ``pygim.path("x.md").read()`` and ``pathlike.markdown``.

The parser is held to the CommonMark 0.31.2 spec and the GFM extension
examples (vendored under tests/unittests/data/markdown), compared the way the
spec's own runner compares; the rest pins what the Document API promises:
lines and spans that slice the source, lossless edits, front matter through
the YAML and TOML engines, and builders whose output parses back.
"""

import importlib.util
import json
import pathlib
import random
import time
import unicodedata

import pytest

import pygim
from pygim import pathlike

md = pathlike.markdown

DATA = pathlib.Path(__file__).parent / "data" / "markdown"


def _write(temp_dir, name, text):
    p = temp_dir / name
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
    out = pygim.path(temp_dir / "copy.md")
    out.write(doc)
    assert out.read_bytes() == f.read_bytes()
    out.write("# Plain text\n")
    assert out.read_bytes() == b"# Plain text\n"


def test_write_refuses_what_is_not_markdown(temp_dir):
    with pytest.raises(TypeError, match="markdown Document or str"):
        pygim.path(temp_dir / "x.md").write({"a": 1})


def test_a_parse_never_fails_but_invalid_utf8_does(temp_dir):
    f = temp_dir / "bad.md"
    f.write_bytes(b"# ok\n\xff\xfe broken\n")
    with pytest.raises(RuntimeError, match="not valid UTF-8"):
        pygim.path(f).read()


# --------------------------------------------------------------------------- #
# Blocks: kinds, lines, spans that slice the source
# --------------------------------------------------------------------------- #
def test_blocks_are_the_top_level_structure():
    doc = md.Document(NOTES)
    assert [b.kind for b in doc.blocks] == [
        "front_matter", "heading", "paragraph", "heading", "paragraph", "code",
        "heading", "table", "list", "quote", "definition",
    ]


def test_every_block_slices_the_source_by_span_and_lines():
    doc = md.Document("é\n\n" + NOTES.split("---\n", 2)[2])   # a non-ASCII byte before everything
    lines = doc.text.splitlines(keepends=True)
    for b in doc.walk():
        start, end = b.span
        assert doc.text[start:end] == b.text
        first, last = b.lines
        assert b.text == "".join(lines[first - 1:last]) or b.parent is not None


def test_walk_is_depth_first_and_children_know_their_parent():
    doc = md.Document("> - a\n>   - b\n")
    kinds = [b.kind for b in doc.walk()]
    assert kinds == ["quote", "list", "item", "paragraph", "list", "item", "paragraph"]
    inner = doc.find("paragraph")[1]
    assert inner.plain == "b" and inner.parent.kind == "item" and inner.parent.parent.kind == "list"
    assert doc.blocks[0].parent is None


def test_headings_have_level_title_and_unique_github_slugs():
    doc = md.Document(NOTES)
    hs = doc.find("heading")
    assert [(h.level, h.title, h.slug) for h in hs] == [(1, "Notes", "notes"), (2, "Usage", "usage"), (2, "Usage", "usage-1")]
    assert doc.find("heading", level=2) == hs[1:]


def test_toc_slugs_match_python_markdowns_anchors():
    doc = md.Document("# Ünïcode  café\n\n# Ünïcode  café\n\n# C++ & C#\n", slugs="toc")
    assert [h.slug for h in doc.find("heading")] == ["unicode-cafe", "unicode-cafe_1", "c-c"]
    github = md.Document("# Ünïcode  café\n\n# C++ & C#\n")
    assert [h.slug for h in github.find("heading")] == ["ünïcode--café", "c--c"]


def test_hashes_inside_fenced_code_are_not_headings():
    doc = md.Document("# Real\n\n```md\n# Fake\n## 1. Fake\n```\n\n    # indented\n")
    assert [h.title for h in doc.find("heading")] == ["Real"]
    assert [s.title for s in doc.sections] == ["Real"]


def test_setext_headings_are_headings():
    doc = md.Document("Title\n=====\n\nSub\n---\n")
    assert [(h.level, h.title) for h in doc.find("heading")] == [(1, "Title"), (2, "Sub")]
    assert doc.find("heading")[0].lines == (1, 2)


def test_code_blocks_give_language_info_and_code():
    doc = md.Document(NOTES)
    (code,) = doc.find("code")
    assert (code.lang, code.info, code.code) == ("bash", "bash", "oo enact read\n")
    assert doc.find("code", lang="bash") == [code] and doc.find("code", lang="python") == []
    nested = md.Document("- item\n\n  ```py title=\"x\"\n  a = 1\n    b\n  ```\n").find("code")[0]
    assert (nested.lang, nested.info, nested.code) == ("py", 'py title="x"', "a = 1\n  b\n")
    indented = md.Document("    x = 1\n").find("code")[0]
    assert (indented.lang, indented.info, indented.code) == (None, None, "x = 1\n")


def test_tables_give_header_rows_and_alignment_as_plain_text():
    (t,) = md.Document(NOTES).find("table")
    assert t.header == ["Term", "Meaning"]
    assert t.rows == [["a|b", "bold"]]
    assert t.align == ["left", "right"]


def test_lists_and_task_items():
    doc = md.Document(NOTES)
    (lst,) = doc.find("list")
    assert (lst.ordered, lst.start, lst.tight) == (False, None, True)
    assert [i.checked for i in lst.children] == [True, False]
    assert [i.plain for i in lst.children] == ["done", "open"]
    ordered = md.Document("3. a\n\n4. b\n").find("list")[0]
    assert (ordered.ordered, ordered.start, ordered.tight) == (True, 3, False)
    assert md.Document("- a\n").find("item")[0].checked is None


def test_quotes_contain_blocks():
    (q,) = md.Document(NOTES).find("quote")
    assert [c.kind for c in q.children] == ["paragraph"]
    assert q.plain == "quoted\ntext"


def test_definitions_are_blocks_and_resolve_references():
    doc = md.Document(NOTES + "\nSee [the site][ref].\n")
    (d,) = doc.find("definition")
    assert (d.label, d.destination, d.title) == ("ref", "https://example.com", "Example")
    assert doc.find("paragraph")[-1].plain == "See the site."
    assert '<a href="https://example.com" title="Example">the site</a>' in doc.html()


def test_plain_text_resolves_inline_markup():
    doc = md.Document("A *b* **c** `d` [e](/f) ![g](h.png) <span>i</span> &amp; \\*j\\*\n")
    assert doc.blocks[0].plain == "A b c d e g i & *j*"
    assert doc.blocks[0].content == "A *b* **c** `d` [e](/f) ![g](h.png) <span>i</span> &amp; \\*j\\*"
    assert md.Document(NOTES).plain.startswith("Notes\n\nIntro with emphasis and code.")


def test_crlf_line_endings_change_nothing_but_the_bytes(temp_dir):
    f = temp_dir / "crlf.md"
    f.write_bytes(NOTES.replace("\n", "\r\n").encode("utf-8"))   # written as bytes: a text write would not test it
    crlf = pygim.path(f).read()
    lf = md.Document(NOTES)
    assert [(b.kind, b.lines, b.plain) for b in crlf.walk()] == [(b.kind, b.lines, b.plain) for b in lf.walk()]
    assert crlf.front_matter == lf.front_matter
    assert crlf.find("code")[0].code == "oo enact read\n"
    for b in crlf.walk():
        assert crlf.text[b.span[0]:b.span[1]] == b.text


def test_stats_report_exact_bytes():
    doc = md.Document(NOTES)
    s = doc.stats()
    assert s["source"] == len(NOTES.encode("utf-8")) and s["blocks"] == len(doc.walk()) and s["lines"] == NOTES.count("\n")
    assert s["bytes"] > s["source"]


def test_repr_names_kind_and_lines():
    doc = md.Document(NOTES)
    assert repr(doc.blocks[1]) == "Block(heading, lines 5-5, 'Notes')"
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
    import datetime

    assert doc.front_matter == {"title": "T", "date": datetime.date(2026, 10, 2)}


def test_no_front_matter_is_none_and_an_unclosed_fence_is_a_rule():
    assert md.Document("# x\n").front_matter is None
    doc = md.Document("---\nnot closed\n")
    assert doc.front_matter is None and doc.blocks[0].kind == "thematic_break"
    assert md.Document(NOTES, front_matter=False).blocks[0].kind == "thematic_break"


def test_invalid_front_matter_names_the_lines_and_the_file(temp_dir):
    f = _write(temp_dir, "bad.md", "---\nkey: [unclosed\n---\n# x\n")
    doc = pygim.path(f).read()
    assert doc.find("heading")[0].title == "x"   # the body still reads
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


# --------------------------------------------------------------------------- #
# Sections and lossless edits
# --------------------------------------------------------------------------- #
def test_sections_nest_by_level_and_own_their_lines():
    doc = md.Document(NOTES)
    notes, usage, usage1 = doc.sections
    assert (notes.title, notes.level, notes.slug, notes.lines) == ("Notes", 1, "notes", (5, 29))
    assert (usage.lines, usage1.lines) == ((9, 16), (17, 29))
    assert notes.subsections == [usage, usage1] and usage.parent == notes and notes.parent is None
    assert [b.kind for b in usage.blocks] == ["paragraph", "code"]
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
    with pytest.raises(ValueError, match="top-level"):
        doc.replace(doc.find("paragraph")[0], "x")
    with pytest.raises(ValueError, match="another document"):
        doc.replace(md.Document("x\n").blocks[0], "y")


# --------------------------------------------------------------------------- #
# HTML: the spec's own examples, compared as its runner compares them
# --------------------------------------------------------------------------- #
COMMONMARK = json.loads((DATA / "commonmark-0.31.2.json").read_text(encoding="utf-8"))
GFM = json.loads((DATA / "gfm-0.29-extensions.json").read_text(encoding="utf-8"))


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


@pytest.mark.parametrize("name,src", [
    ("nested emphasis", "*a **a " * 20000 + "b** b*" * 20000),
    ("nested quotes", "> " * 20000 + "a"),
    ("unclosed links", "[a](b" * 20000),
    ("unclosed comments", "</" + "<!--" * 20000),
    ("image link openers", "![[]()" * 20000),
    ("nested brackets", "[" * 20000 + "a" + "]" * 20000),
    ("backtick runs", "".join("e" + "`" * i for i in range(1, 400))),
])
def test_pathological_input_is_linear(name, src):
    t = time.perf_counter()
    md.Document(src).html()
    assert time.perf_counter() - t < 0.5, name


# --------------------------------------------------------------------------- #
# Writing markdown: builders whose output parses back
# --------------------------------------------------------------------------- #
TRICKY = ["a*b*c", "[x](y)", "# not a heading", "1. not a list", "- not a list", "> not a quote", "a | b",
          "`code`", "&amp; stays", "<b>bold</b>", "_under_", "~~strike~~", "back\\slash", "100% + 1 = 2",
          "=== not setext", "***", "!", "C#", "x # y #", "Ünïcode ✓"]


@pytest.mark.parametrize("text", TRICKY)
def test_escaped_text_reads_back_as_itself(text):
    assert md.Document(md.escape(text)).blocks[0].plain == text
    assert md.Document(md.heading(2, md.escape(text))).find("heading")[0].title == text


def test_heading_validates_its_level():
    assert md.heading(3, "T") == "### T\n"
    with pytest.raises(ValueError, match="level must be 1-6, got 7"):
        md.heading(7, "T")


def test_code_picks_a_fence_its_code_cannot_close():
    text = "```\nnot the end\n````\n"
    block = md.code(text, lang="md")
    assert block.startswith("`````md\n")
    (c,) = md.Document(block).find("code")
    assert (c.code, c.lang) == (text, "md")
    assert md.Document(md.code("x", lang="weird`lang")).find("code")[0].info == "weird`lang"


def test_table_round_trips_its_cells_and_lines_up():
    rows = [["a|b", 1], ["multi\nline", "`x|y`"], ["short"]]
    text = md.table(["Key", "Value"], rows, align=["left", "right"])
    assert text.splitlines()[:2] == ["| Key           |  Value |", "| :------------ | -----: |"]   # widest cell: multi<br>line
    (t,) = md.Document(text).find("table")
    assert t.header == ["Key", "Value"] and t.align == ["left", "right"]
    assert t.rows == [["a|b", "1"], ["multiline", "x|y"], ["short", ""]]
    with pytest.raises(ValueError, match="align"):
        md.table(["a"], [], align=["middle"])


def test_bullets_quote_and_join_build_documents():
    text = md.join([md.heading(1, "T"), md.bullets(["one", "two\nlines"]), md.bullets(["a", "b"], numbered=True, start=3),
                    md.quote("q\n\nr"), ""])
    doc = md.Document(text)
    assert [b.kind for b in doc.blocks] == ["heading", "list", "list", "quote"]
    assert [i.plain for i in doc.blocks[1].children] == ["one", "two\nlines"]
    assert doc.blocks[2].start == 3 and doc.blocks[3].plain == "q\n\nr"
    assert text.endswith("> r\n") and "\n\n\n" not in text


def test_front_matter_builder_uses_the_engines():
    assert md.front_matter({"a": 1}) == "---\na: 1\n---\n"
    assert md.front_matter({"a": 1}, engine="toml") == "+++\na = 1\n+++\n"
    with pytest.raises(ValueError, match="yaml or toml"):
        md.front_matter({"a": 1}, engine="json")


# --------------------------------------------------------------------------- #
# The SIMD stop scan equals the scalar one; the generated tables are current
# --------------------------------------------------------------------------- #
def test_simd_stop_scan_equals_the_scalar_reference():
    rng = random.Random(20261002)
    alphabet = "ab \n\\`*_[]!<&~é|#"
    texts = [ex["markdown"] for ex in COMMONMARK]
    texts += ["".join(rng.choice(alphabet) for _ in range(rng.randrange(0, 300))) for _ in range(300)]
    texts += ["x" * n + "*" for n in range(0, 140)]   # a stop at every position around the 16- and 64-byte edges
    for t in texts:
        assert md._stops(t, "simd") == md._stops(t, "scalar"), repr(t)
    assert md._stops("a *b*", "scalar") == [2, 4]


def test_generated_unicode_tables_are_current():
    root = pathlib.Path(__file__).parents[2]
    gen = root / "tests" / "static" / "gen_markdown_tables.py"
    header = root / "src" / "_pygim_fast" / "pathlike" / "markdown" / "tables.h"
    if not header.is_file():
        pytest.skip("source tree not present (installed wheel)")
    text = header.read_text(encoding="utf-8")
    if f"Unicode {unicodedata.unidata_version} " not in text:
        pytest.skip(f"tables.h records another Unicode version than this interpreter's {unicodedata.unidata_version}")
    spec = importlib.util.spec_from_file_location("gen_markdown_tables", gen)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert mod.render() == text, "stale: run tests/static/gen_markdown_tables.py"
