# type: ignore
"""Markdown: read a document, find what is in it, edit it losslessly, write new markdown.

``pygim.path("x.md").read()`` returns a ``pathlike.markdown.Document`` — the
exact text and its block tree — rather than dicts and lists, because markdown
is a document, not a value. Every block knows its lines and its span (character
offsets into the text), so code that used to search markdown with regular
expressions asks the tree instead, and an edit changes only the bytes it means.

This example demonstrates:
- Reading a .md file: front matter through the YAML engine, blocks, headings
- Finding things: sections by slug, fenced code by language, table cells
- A lossless edit: one section replaced, every other byte unchanged
- Writing markdown: escape(), table(), code(), bullets(), join()
- HTML, as CommonMark's reference renderer writes it
"""

import tempfile
from pathlib import Path

import pygim
from pygim import pathlike

md = pathlike.markdown

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

notes = root / "notes.md"
notes.write_bytes(b"""\
---
title: Release notes
version: 2
---
# Release notes

## Install

```bash
pip install pygim
```

## Formats

| Format | Engine |
| :----- | :----- |
| YAML   | rapidyaml |
| JSON   | simdjson |

## Install

A second heading with the same title gets its own anchor.
""")

# ----------------------------------------------------------------------------
# 1. Read: a Document, its front matter, its blocks
# ----------------------------------------------------------------------------
doc = pygim.path(notes).read()                       # an mdpath: .md dispatches to the markdown engine
assert isinstance(doc, md.Document)
assert doc.text == notes.read_text(encoding="utf-8")  # the exact text, nothing normalised
assert doc.front_matter == {"title": "Release notes", "version": 2}   # YAML, through pathlike's YAML engine
assert [b.kind for b in doc.blocks][:3] == ["front_matter", "heading", "heading"]

# ----------------------------------------------------------------------------
# 2. Find: headings with anchors, sections, code by language, table cells
# ----------------------------------------------------------------------------
assert [h.slug for h in doc.find("heading")] == ["release-notes", "install", "formats", "install-1"]
formats = doc.section("formats")                     # by slug (or by title)
assert formats.lines == (13, 19)                     # the heading and everything up to the next ## (blank lines included)
(table,) = doc.find("table")
assert table.header == ["Format", "Engine"] and table.rows[1] == ["JSON", "simdjson"]
(code,) = doc.find("code", lang="bash")
assert code.code == "pip install pygim\n" and code.lines == (9, 11)
assert doc.text[code.span[0]:code.span[1]] == code.text   # spans slice the text Python holds

# A '#' inside a fenced block is code, not a heading — the tree knows, a regex does not:
assert md.Document("```md\n# not a heading\n```\n").find("heading") == []

# ----------------------------------------------------------------------------
# 3. Edit losslessly: replace one section, nothing else moves
# ----------------------------------------------------------------------------
new_table = md.table(["Format", "Engine"], [["YAML", "rapidyaml"], ["JSON", "simdjson"], ["Markdown", "pygim-md"]])
edited = doc.replace(formats, md.join([md.heading(2, "Formats"), new_table]))
before, after = formats.span
assert edited.text.startswith(doc.text[:before]) and edited.text.endswith(doc.text[after:])
assert edited.find("table")[0].rows[-1] == ["Markdown", "pygim-md"]
edited = edited.with_front_matter({"title": "Release notes", "version": 3})   # rewritten by the YAML engine
pygim.path(notes).write(edited)                       # a Document (or a str) writes its text
assert pygim.path(notes).read().front_matter["version"] == 3

# ----------------------------------------------------------------------------
# 4. Write markdown: builders take markdown, escape() makes plain text safe
# ----------------------------------------------------------------------------
title = "Totals for *all* users | 2026"                # plain text with markdown's own characters in it
text = md.join([
    md.heading(1, md.escape(title)),
    md.bullets(["parsed", "rendered"], numbered=True),
    md.code("print('`ticks` inside')\n", lang="python"),   # the fence is chosen so no line can close it
])
out = md.Document(text)
assert out.find("heading")[0].title == title          # escaped text reads back as itself
assert out.find("list")[0].start == 1 and out.find("code")[0].lang == "python"

# ----------------------------------------------------------------------------
# 5. HTML, as the CommonMark reference renderer writes it
# ----------------------------------------------------------------------------
assert md.Document("Some *emphasis* and `code`.\n").html() == "<p>Some <em>emphasis</em> and <code>code</code>.</p>\n"

tmp.cleanup()
