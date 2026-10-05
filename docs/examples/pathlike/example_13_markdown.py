# type: ignore
"""Markdown: read a document, find what is in it, edit it losslessly, write new markdown.

``pygim.path("x.md").read()`` returns a ``pathlike.markdown.Document`` — the
exact text and its block tree — rather than dicts and lists, because markdown
is a document, not a value. Every block knows its lines and its span (character
offsets into the text), so code that used to search markdown with regular
expressions asks the tree instead, and an edit changes only the bytes it means.

Each block is an instance of its kind's class — ``Heading``, ``Code``,
``Table``, ... — which holds only that kind's properties.

This example demonstrates:
- Reading a .md file: front matter through the YAML engine, blocks of their own classes
- Finding things: sections by slug, fenced code by language, table cells
- A lossless edit: one section replaced, every other byte unchanged
- Writing markdown: escape(), table(), code(), bullets(), join()
- HTML, as CommonMark's reference renderer writes it
"""

import tempfile

import pygim
from pygim import pathlike

md = pathlike.markdown

tmp = tempfile.TemporaryDirectory()          # pygim has no scratch directory of its own
notes = pygim.path(tmp.name) / "notes.md"    # an mdpath: .md selects the markdown engine
notes.write("""\
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
doc = notes.read()
assert isinstance(doc, md.Document)

# The text is the file's, byte for byte: nothing is normalised.
assert doc.text.encode("utf-8") == notes.read_bytes()

# Front matter is read by pathlike's YAML engine.
assert doc.front_matter == {"title": "Release notes", "version": 2}

# Each top-level block is an instance of its kind's class.
kinds = [type(block).__name__ for block in doc.blocks]
assert kinds == ["FrontMatter", "Heading", "Heading", "Code",
                 "Heading", "Table", "Heading", "Paragraph"]

# A class holds only its kind's properties: a Heading has .level, a Table .rows.
title = doc.blocks[1]
assert title.level == 1
assert not hasattr(title, "rows")

# ----------------------------------------------------------------------------
# 2. Find: headings with anchors, sections, code by language, table cells
# ----------------------------------------------------------------------------
#                   ┌─ cls: a block class; its blocks, in document order
#                   ▼
headings = doc.find(md.Heading)
slugs = [h.slug for h in headings]
assert slugs == ["release-notes", "install", "formats", "install-1"]

#                     ┌─ key: a slug, or a heading's title
#                     ▼
formats = doc.section("formats")
assert formats.title == "Formats"
assert formats.lines == (13, 19)       # up to the next ##, its blank line included

table = doc.find(md.Table)[0]
assert table.header == ["Format", "Engine"]
assert table.rows[1] == ["JSON", "simdjson"]

bash = [c for c in doc.find(md.Code) if c.lang == "bash"][0]
assert bash.code == "pip install pygim\n"
assert bash.lines == (9, 11)

# A span is (start, end) in characters, so it slices the text Python holds.
start, end = bash.span
assert doc.text[start:end] == bash.text

# A '#' inside a fenced block is code, not a heading: the tree knows, a regex does not.
fenced = md.Document("```md\n# not a heading\n```\n")
assert fenced.find(md.Heading) == []

# ----------------------------------------------------------------------------
# 3. Edit losslessly: replace one section, nothing else moves
# ----------------------------------------------------------------------------
new_table = md.table(
    ["Format", "Engine"],
    [["YAML", "rapidyaml"], ["JSON", "simdjson"], ["Markdown", "pygim-md"]],
)
new_section = md.join([md.heading(2, "Formats"), new_table])

#                    ┌─ target: a top-level block or a section
#                    │        ┌─ text: the markdown that takes its place
#                    ▼        ▼
edited = doc.replace(formats, new_section)

start, end = formats.span
assert edited.text[:start] == doc.text[:start]       # every byte before: unchanged
assert edited.text.endswith(doc.text[end:])          # every byte after: unchanged
assert edited.find(md.Table)[0].rows[-1] == ["Markdown", "pygim-md"]

# The front matter is rewritten by the YAML engine; the body is untouched.
edited = edited.with_front_matter({"title": "Release notes", "version": 3})
notes.write(edited)                                  # a Document (or a str) writes its text
assert notes.read().front_matter["version"] == 3

# ----------------------------------------------------------------------------
# 4. Write markdown: builders take markdown, escape() makes plain text safe
# ----------------------------------------------------------------------------
heading_text = "Totals for *all* users | 2026"     # plain text holding markdown's characters
heading = md.heading(1, md.escape(heading_text))
assert heading == "# Totals for \\*all\\* users \\| 2026\n"

#                                          ┌─ numbered: "1." items instead of "-"
#                                          ▼
steps = md.bullets(["parsed", "rendered"], numbered=True)
assert steps == "1. parsed\n2. rendered\n"

# The fence is one longer than any backtick run in the code, so no line can close it.
#                                      ┌─ lang: the fence's info string
#                                      ▼
sample = md.code("```\nnested\n```\n", lang="md")
assert sample == "````md\n```\nnested\n```\n````\n"

out = md.Document(md.join([heading, steps, sample]))
assert out.find(md.Heading)[0].title == heading_text     # escaped text reads back as itself
assert out.find(md.Code)[0].code == "```\nnested\n```\n"

# ----------------------------------------------------------------------------
# 5. HTML, as the CommonMark reference renderer writes it
# ----------------------------------------------------------------------------
html = md.Document("Some *emphasis* and `code`.\n").html()
assert html == "<p>Some <em>emphasis</em> and <code>code</code>.</p>\n"

tmp.cleanup()
print("pathlike markdown example OK:", slugs)
