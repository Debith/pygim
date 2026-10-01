# -*- coding: utf-8 -*-
"""Static docs server with a review layer (``oo docs serve``).

Serves a directory of HTML/CSS/JS and adds two things a plain
``python -m http.server`` lacks:

* **Comments.** Every served ``.html`` page gets the ✎ commenter fragment
  (``_commenter.py``). Comments POST to ``/comment`` and live, one JSON
  document per line, in ``<root>/__notes__/site-comments.jsonl`` — read and
  written by pathlike's JSONL engine. ``GET /comments`` lists the open ones
  (``?page=<path>`` filters); ``/comment-edit`` and ``/comment-delete`` change
  them in place. A malformed comments file is reported (HTTP 500 with the
  file and line), never silently rewritten.
* **Markdown.** Opening ``x.md`` GENERATES ``x.generated.html`` beside it (the
  ``markdown`` package; Mermaid fences become live diagrams; ``.md`` links are
  rewritten to their generated page) and redirects there, so the commenter works
  on the HTML page and comments key on it — the same shape as a built site. The
  ``.generated.html`` name says the file is derived and disposable, so one glob
  ignores every one of them. It is regenerated when the Markdown is newer and
  carries a marker; a hand-written ``x.html`` is a page in its own right and is
  never overwritten. ``README.md`` / ``index.md`` stand in for a missing
  ``index.html``.
* **What changed since you read it.** Every page shows when it last changed — the file's
  time, and what git says: not committed, or the last commit. A page made from Markdown can
  be marked read (``POST /read``): its Markdown is kept, as it was, in
  ``<root>/__notes__/read/<its path>.json``, and ``GET /read-state?page=<path>`` then lists
  the blocks added and changed since — a changed one with what it said before — and the
  blocks removed. A generated page tags each top-level block with ``data-block="<n>-<digest>"``
  so the page can highlight them. A folder's listing marks each file that changed since it was
  marked read, or that git has not committed, and gives each folder the count of such files
  anywhere below it.
* **Image drops.** Dropping an image on a page POSTs it to
  ``/upload?path=images/<sub>/<file>`` and it is written straight into
  ``<root>/images/<sub>/<file>`` — no Downloads round-trip. Uploads land only
  under an ``images`` directory and only with an image extension.

File handling is pathlike (``pygim.path``); Python here is routing glue. The
server owns a ``PathStore``: every path it makes — the root, the comments file,
each request's translated path, the pages inventory — is a row of that one
table, so the module's default store never sees request traffic and the whole
table goes when the server is dropped. ``GET /pages`` lists the site's HTML
pages (a ``PathSet`` of ``root.pathset("**/*.html")``), ``?page=`` is compared
as a path (spellings collapse), and ``rebuild()`` reports what a rebuild added
and removed as a set difference.

LOCAL USE: binds all interfaces by default (so the page is reachable via the WSL
IP when Windows→WSL localhost forwarding hiccups) and only ever writes under the
served root. Don't expose it to an untrusted network.
"""

from __future__ import annotations

import datetime
import difflib
import errno
import functools
import hashlib
import http.server
import io
import json
import html
import os
import posixpath
import re
import socket
import socketserver
import urllib.parse

import pygim
from pygim.pathlike import PathStore
from _pygim._cli import _commenter

__all__ = ["ServeError", "Watch", "blocks_of", "history_of", "make_server", "materialize_markdown", "read_state",
           "rebuild", "render_markdown", "serve", "site_pages"]

SEG_RE = re.compile(r"^[a-z0-9][a-z0-9 ._-]*$", re.IGNORECASE)  # one path segment, no traversal (spaces ok)
EXT_OK = (".jpg", ".jpeg", ".png", ".webp", ".gif")
MAX_BYTES = 25 * 1024 * 1024  # 25 MB per image
MAX_COMMENT = 64 * 1024  # 64 KB per comment payload

# Site annotations land here (relative to the served root): one JSON object per line.
COMMENTS_REL = "__notes__/site-comments.jsonl"
# A page's Markdown as it was when its reader marked it read: ``<READ_REL>/<page's path>.json``.
READ_REL = "__notes__/read"
# How alike an old and a new block must be (difflib's ratio) to be one block changed rather than
# one removed and another added.
SAME_BLOCK = 0.5

# Where ``/`` goes when the served root has no ``index.html`` of its own: the
# first of these that exists (common generated-docs layouts).
INDEX_CANDIDATES = (
    "site/index.html",
    "docs/index.html",
    "build/html/index.html",
    "docs/_build/html/index.html",
    "_build/html/index.html",
    "README.md",
    "index.md",
    "00_overview.md",
)
PAGE_SUFFIXES = (".html", ".md")

MARKDOWN_STYLE = """<style>
body{max-width:72ch;margin:2rem auto;padding:0 1rem;font:16px/1.55 system-ui,sans-serif;color:#1d242b;background:#fbfbfa}
pre,code{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.92em}
pre{background:#f0f1ee;padding:.8rem 1rem;overflow-x:auto;border-radius:4px}
code{background:#f0f1ee;padding:.1em .3em;border-radius:3px}
pre code{background:none;padding:0}
table{border-collapse:collapse;margin:1rem 0}
th,td{border-bottom:1px solid #d3d7d2;padding:.35rem .6rem;text-align:left;vertical-align:top}
th{color:#5f6a72;font-size:.85em;letter-spacing:.04em;text-transform:uppercase}
h1,h2,h3{line-height:1.2}
blockquote{border-left:3px solid #b06e14;margin:1rem 0;padding:.2rem 1rem;color:#5f6a72}
.mermaid{background:none}
code.term,abbr.term{border-bottom:1px dotted #8a9299;cursor:help}
th.term{border-bottom:1px dotted #8a9299;cursor:help}
a.xref{color:inherit;text-decoration:none;border-bottom:1px dotted #8a9299;cursor:help}
.rd-meta{min-height:1.45em;margin:-.3rem 0 1.2rem;color:#5f6a72;font-size:.85rem;line-height:1.45;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
</style>"""
# Raised whenever the renderer's output changes, so a page an older renderer made is made again
# rather than kept until its Markdown next changes.
RENDERER = "renderer 5"
# Under a generated page's title: when the page changed, and the way to mark it read (``_commenter.READER``).
META_LINE = '<p id="rd-meta" class="rd-meta"></p>'
GENERATED_MARK = "<!-- generated from {src} by oo docs serve (" + RENDERER + "); edit the Markdown, not this file -->"
# A generated page is named for what it is, so it reads as derived in any listing and one
# glob ignores the lot. A hand-written ``x.html`` keeps its plain name and is never touched.
GENERATED_SUFFIX = ".generated.html"
MERMAID_SCRIPT = ("<script type=\"module\">import mermaid from "
                  "\"https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs\";"
                  "mermaid.initialize({startOnLoad:true});</script>")

HOST_ENV = "PYGIM_HOST"
# This run of the server; a page that sees another one at /alive reloads, to get the new code.
BOOT = os.urandom(8).hex()

# A table whose first heading cell is one of these defines terms: column one is the
# term, column two its meaning. Writing such a table is all a page does to get hover
# text on every code span naming one of its terms, anywhere on the site.
TERM_HEADS = ("term", "type")

_HEADING_RE = re.compile(r"^#{2,4}\s+(\d+(?:\.\d+)*)\.?\s+(.+?)\s*$", re.M)
_RULE_RE = re.compile(r"^\|[\s:|-]+\|$")


def _md_plain(cell: str) -> str:
    """A Markdown table cell as plain text: links, emphasis and code ticks removed."""
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", cell)
    return text.replace("**", "").replace("`", "").replace("*", "").strip()


def _anchor(heading: str) -> str:
    """The id python-markdown's toc extension gives *heading* ("2.3 A tag" -> "23-a-tag")."""
    text = re.sub(r"[^\w\s-]", "", heading).strip().lower()
    return re.sub(r"[-\s]+", "-", text)


def _terms_of(text: str) -> dict:
    """Every term a Term/Type table in *text* defines, mapped to its meaning."""
    terms, rows, i = {}, text.split("\n"), 0
    while i < len(rows) - 1:
        if rows[i].startswith("|") and _RULE_RE.match(rows[i + 1].strip()):
            head = [c.strip() for c in rows[i].strip().strip("|").split("|")]
            if head and _md_plain(head[0]).lower() in TERM_HEADS:
                i += 2
                while i < len(rows) and rows[i].startswith("|"):
                    cells = [c.strip() for c in rows[i].strip().strip("|").split("|")]
                    term = _md_plain(cells[0]) if cells else ""
                    meaning = _md_plain(cells[1]) if len(cells) > 1 else ""
                    if term and meaning:
                        terms.setdefault(term, meaning)
                    i += 1
                continue
        i += 1
    return terms


class _SiteIndex:
    """What the Markdown pages under a root define between them: numbered sections
    other pages cite as a section reference, and the terms their Term/Type tables
    describe. Built once at startup, and a page written later joins it when it is first
    rendered; the renderer turns both into hover text."""

    def __init__(self, root=None):
        self.root = root
        self.terms = {}        # term -> meaning
        self.sections = {}     # "4.8" -> [(page href, full heading)]
        self.pages = {}        # markdown path -> href of the HTML generated for it

    @classmethod
    def build(cls, root):
        index = cls(root)
        for page in sorted(site_pages(root), key=lambda p: p.name):
            index.add(page)
        return index

    def add(self, page):
        """Index one Markdown page — the href of the page generated for it, its numbered
        sections, its terms — and return that href; None for a page it cannot read."""
        if self.root is None or page.suffix != ".md":
            return None
        try:
            text = _page_text(page)
        except (OSError, RuntimeError, UnicodeDecodeError):
            return None
        href = _relative(self.root, page.with_suffix(GENERATED_SUFFIX))
        self.pages[os.fspath(page)] = href
        for number, heading in _HEADING_RE.findall(text):
            self.sections.setdefault(number, []).append((href, f"{number} {heading}".strip()))
        for term, meaning in _terms_of(text).items():
            self.terms.setdefault(term, meaning)
        return href

    def link(self, number: str, page: str | None, qualifier: str | None = None, folder: str | None = None):
        """A section reference as a link into the page that defines it. A reference
        that names its page — "section 03 §2.1", "overview §4.8" — goes there or
        nowhere; a bare one goes to this page first, then to the first page by name,
        which is the overview a bare reference in a later section means. Two folders can
        each hold a page of that name, and then the copy nearest this page is the one
        meant (``_nearest``) — unless the reference names the folder, "ENACT 00 §5", and a
        copy lives in a folder of that name. A word that names no such folder is prose."""
        hits = self.sections.get(number)
        if not hits:
            return None
        if qualifier:
            named = [hit for hit in hits if _names_page(qualifier, hit[0])]
            if not named and qualifier == "overview":
                named = hits[:1]        # no page is called that: the first page is the overview
            if not named:
                return None
            if folder:
                inside = [hit for hit in named
                          if folder.lower() in (part.lower() for part in hit[0].strip("/").split("/")[:-1])]
                named = inside or named
            href, heading = _nearest(named, page)
        else:
            href, heading = next((hit for hit in hits if hit[0] == page), None) or _nearest(hits, page)
        return _xref(href, heading, number, page)

    def file_link(self, number: str, page: str | None, file: str):
        """A section reference that names its page by file — "`brief.md` §2",
        "`docs/design/enact/00_overview.md` §4.7" — as a link into that file, matched by as
        much of its path as the site holds; None when the site has no such file defining it."""
        hits = self.sections.get(number) or []
        parts = [part for part in file.replace("\\", "/").split("/") if part not in ("", ".")]
        parts[-1] = parts[-1][:-len(".md")] + GENERATED_SUFFIX
        for start in range(len(parts)):
            tail = "/" + "/".join(parts[start:])
            named = [hit for hit in hits if hit[0].endswith(tail)]
            if named:
                return _xref(*_nearest(named, page), number, page)
        return None


def _xref(href: str, heading: str, number: str, page: str | None) -> str:
    """The link to section *number* of the page at *href*, relative to *page*."""
    if page is None:
        target = href                            # no page to be relative to: from the site's root
    else:
        target = "" if href == page else posixpath.relpath(href, posixpath.dirname(page))
    return (f'<a class="xref" href="{target}#{_anchor(heading)}"'
            f' title="{html.escape(heading, quote=True)}">&sect;{number}</a>')


def _nearest(hits: list, page: str | None):
    """The hit to link: the first by file name, as the index is ordered — and when pages of
    that same name sit in several folders (``design/enact/00_overview.md`` and
    ``design/task/00_overview.md``), the copy nearest *page*: the most folders in common with
    it, then the fewest folders below those. Nearness never picks a different document, only
    which copy of the one the name picked."""
    name = posixpath.basename(hits[0][0])
    copies = [hit for hit in hits if posixpath.basename(hit[0]) == name]
    if not page or len(copies) == 1:
        return hits[0]
    here = posixpath.dirname(page).strip("/").split("/")

    def distance(hit):
        there = posixpath.dirname(hit[0]).strip("/").split("/")
        shared = 0
        for a, b in zip(here, there):
            if a != b:
                break
            shared += 1
        return -shared, len(there) - shared

    return min(copies, key=distance)


class ServeError(RuntimeError):
    """The server could not start — the message carries the user-facing hint block."""


def _now() -> str:
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def _upload_target(root, qs):
    """The safe absolute path for an ``?path=<root-relative>`` (or legacy
    ``?name=<file>``) upload query, or None. The path must pass through an
    ``images`` directory and end in an image extension; every segment is
    validated (no ``..``), and the resolved target must stay under *root*."""
    rel = qs.get("path", [None])[0]
    if rel is None:
        rel = "images/" + pygim.path(qs.get("name", [""])[0]).name
    segs = [s for s in rel.replace("\\", "/").split("/") if s not in ("", ".")]
    if len(segs) < 2 or "images" not in segs[:-1] or not all(SEG_RE.match(s) for s in segs):
        return None
    if not segs[-1].lower().endswith(EXT_OK):
        return None
    dest = root.joinpath(*segs).resolve()
    return (dest, "/".join(segs)) if root in dest.parents else None


def site_pages(root):
    """The site's pages as a PathSet over *root*'s table: every ``*.html`` and
    ``*.md`` under the root except the notes directory. Two calls on the same
    root are two sets over one table, so they subtract and intersect at bit
    speed."""
    notes = COMMENTS_REL.split("/")[0]
    pages = root.pathset("**/*.html") | root.pathset("**/*.md")
    return pages - (root.pathset(notes + "/**/*.html") | root.pathset(notes + "/**/*.md"))


def listed_pages(root):
    """The pages a reader can open: every HTML page, plus Markdown pages that have
    no generated HTML yet (a bit test per page against the same set)."""
    pages = site_pages(root)
    return [p for p in pages if not (p.suffix == ".md" and p.with_suffix(GENERATED_SUFFIX) in pages)]


_TERM_RE = re.compile(r"<(code|strong|th)>([^<>]+)</\1>")
# "§4.8", optionally preceded by the page it lives in: "overview §4.8",
# "section 03 §2.1", "01 §3", "(02, §5.3)" — and that page by its folder: "ENACT 00 §5".
_SECTION_REF_RE = re.compile(
    r"(?P<q>\b(?:(?P<folder>[A-Za-z][\w-]*)\s+(?=\d{2}[a-z]?\b))?"
    r"(?:overview(?:'s)?|(?:section\s+)?\d{2}[a-z]?)\b,?\s+)?\u00a7(?P<n>\d+(?:\.\d+)*)")


# "`brief.md` §2": a file named in a code span, then the section — the renderer's HTML of it.
_FILE_REF_RE = re.compile(r"(<code>(?P<file>[\w./-]+\.md)</code>,?\s+)\u00a7(?P<n>\d+(?:\.\d+)*)")


def _qualifier(text: str | None) -> str | None:
    """The page a reference names, as "overview" or a two-digit prefix, or None."""
    if not text:
        return None
    word = text.strip().rstrip(",").strip().lower()
    word = word.removeprefix("section").strip()
    return "overview" if word.startswith("overview") else word


def _names_page(qualifier: str, href: str) -> bool:
    name = posixpath.basename(href)
    return "overview" in name if qualifier == "overview" else name.startswith(qualifier + "_")
_SPLIT_TAGS_RE = re.compile(r"(<[^>]+>)")


def _linked_reference(match, site, page: str | None) -> str:
    """One section reference, rewritten as a link when its target is known; the
    page qualifier in front of it is kept as text."""
    prefix, folder = match.group("q") or "", match.group("folder")
    link = site.link(match.group("n"), page, _qualifier(prefix[len(folder):] if folder else prefix), folder)
    return prefix + link if link else match.group(0)


def _annotate(body: str, site, page: str | None) -> str:
    """Hover text for the two things a reader stops on: a term the site defines, named
    the way an author names one — in a code span, in bold, or as the header of the
    column it describes — and a section reference. Code blocks and existing links are
    left alone, so nothing inside a fenced example is rewritten."""
    if site is None:
        return body

    def marked_term(match):
        tag, text = match.group(1), match.group(2)
        meaning = site.terms.get(html.unescape(text))
        if not meaning:
            return match.group(0)
        return (f'<{tag} class="term" title="{html.escape(meaning, quote=True)}">'
                f"{text}</{tag}>")

    body = _TERM_RE.sub(marked_term, body)
    # a reference naming its page by file goes there or nowhere: unresolved, its § becomes an
    # entity, which the bare-reference pass below does not read as a section reference
    body = _FILE_REF_RE.sub(lambda m: m.group(1) + (site.file_link(m.group("n"), page, m.group("file"))
                                                    or "&sect;" + m.group("n")), body)

    out, skip = [], 0
    for token in _SPLIT_TAGS_RE.split(body):
        if token.startswith("<"):
            name = token[1:].split(" ", 1)[0].rstrip("/>").lower()
            if name in ("pre", "code", "a"):
                skip += 1
            elif name in ("/pre", "/code", "/a"):
                skip = max(0, skip - 1)
            out.append(token)
        elif skip:
            out.append(token)
        else:
            out.append(_SECTION_REF_RE.sub(lambda m: _linked_reference(m, site, page), token))
    return "".join(out)


_FENCE_OPEN_RE = re.compile(r"^(`{3,}|~{3,})")
_LIST_ITEM_RE = re.compile(r"^(?:[-*+]|\d+[.)])\s")
_BLOCK_MARK_RE = re.compile(r"<!--b:(\d+-[0-9a-f]{12})-->\s*<([a-zA-Z][a-zA-Z0-9]*)")


def _block_spans(text: str) -> list:
    """*text* (Markdown) as its top-level blocks: ``(first line, block text)`` for each paragraph,
    heading, list, table or code block a reader sees as one. A fence is one block whatever it
    holds, and a list stays one block across the blank lines between its items."""
    lines = text.split("\n")
    spans, start, current, fence, blank = [], 0, [], None, False

    def close():
        if current:
            spans.append((start, "\n".join(line.rstrip() for line in current).strip("\n")))
        current.clear()

    for number, line in enumerate(lines):
        if fence:
            current.append(line)
            closing = line.strip()
            if closing.startswith(fence) and not closing.strip(fence[0]):
                fence = None
                close()
            continue
        if not line.strip():
            blank = True
            continue
        opening = _FENCE_OPEN_RE.match(line)
        indented = line[:1] in (" ", "\t")
        listed = bool(current) and _LIST_ITEM_RE.match(current[0]) and _LIST_ITEM_RE.match(line)
        if current and not opening and (not blank or indented or listed):
            if blank:
                current.append("")
        else:
            close()
            start = number
        current.append(line)
        blank = False
        if opening:
            fence = opening.group(1)
    close()
    return spans


def blocks_of(text: str) -> list:
    """*text* (Markdown) as the text of its top-level blocks, in order (see ``_block_spans``)."""
    return [block for _, block in _block_spans(text)]


def _digest(block: str) -> str:
    return hashlib.sha1(block.encode("utf-8")).hexdigest()[:12]


def _tokens(blocks: list) -> list:
    """What names each block in a page: its place and the digest of its text, ``"<n>-<digest>"``,
    so a page made from other text never matches a token by accident."""
    return [f"{n}-{_digest(block)}" for n, block in enumerate(blocks)]


def _with_block_marks(text: str) -> str:
    """*text* with an HTML comment naming each top-level block in front of it, on lines of its
    own, for the renderer to turn into a ``data-block`` attribute on the block's element."""
    spans = _block_spans(text)
    lines = text.split("\n")
    for (first, _), token in reversed(list(zip(spans, _tokens([block for _, block in spans])))):
        lines[first:first] = ["", f"<!--b:{token}-->", ""]
    return "\n".join(lines)


@functools.lru_cache(maxsize=4096)
def _render_fragment(text: str) -> str:
    """A few blocks of Markdown as HTML, for showing what a page said before; a block rendered once
    is not rendered again on the next click."""
    try:
        import markdown
    except ImportError:
        return f"<pre>{html.escape(text)}</pre>"
    return markdown.markdown(text, extensions=["fenced_code", "tables", "md_in_html"])


def render_markdown(text: str, title: str, *, site=None, page: str | None = None) -> str | None:
    """*text* (Markdown) as a complete HTML page, or None when the ``markdown``
    package is not installed (the file is then served as it is). Fenced
    ``mermaid`` blocks become ``<pre class="mermaid">`` with the Mermaid
    script, so diagrams render in the browser. Given a *site* index, a code span or
    table header naming one of its terms and a section reference all gain hover text.
    Each top-level block's element carries ``data-block="<n>-<digest>"``."""
    try:
        import markdown
    except ImportError:
        return None

    # md_in_html: a <details markdown="1"> block folds a long section and keeps what is written inside it Markdown
    body = markdown.markdown(_with_block_marks(text), extensions=["fenced_code", "tables", "toc", "md_in_html"])
    body = _annotate(body, site, page)
    # relative links to Markdown point at the pages generated for them
    body = re.sub(r'(href=")(?![a-z][a-z0-9+.-]*:|/|#)([^"#]+)\.md(#[^"]*)?"',
                  lambda m: f'{m.group(1)}{m.group(2)}{GENERATED_SUFFIX}{m.group(3) or ""}"', body)
    mermaid = ""
    if 'class="language-mermaid"' in body:
        # The block stays escaped: Mermaid decodes the entities of `innerHTML` itself, and an
        # unescaped `<<strategy>>` would be parsed by the browser as an element first.
        body = re.sub(
            r'<pre><code class="language-mermaid">(.*?)</code></pre>',
            lambda m: '<pre class="mermaid">' + m.group(1) + "</pre>",
            body, flags=re.S)
        mermaid = MERMAID_SCRIPT
    body = _BLOCK_MARK_RE.sub(lambda m: f'<{m.group(2)} data-block="{m.group(1)}"', body)
    body = re.sub(r"<!--b:[^>]*-->\s*", "", body)     # a block that rendered to nothing, such as a link definition
    # the line the review layer writes when the page changed into: under the title, there from the start
    if "</h1>" in body:
        body = body.replace("</h1>", "</h1>\n" + META_LINE, 1)
    else:
        body = META_LINE + "\n" + body
    return (f"<!doctype html><html><head><meta charset=\"utf-8\"><title>{html.escape(title)}</title>"
            f"{MARKDOWN_STYLE}</head><body>{body}{mermaid}</body></html>")


def pregenerate(root) -> int:
    """Generate the HTML of every Markdown page under *root* that is missing or
    stale, so no first open pays for a render (a fresh page costs two stats).
    Also imports the renderer once. Returns how many pages were (re)generated."""
    try:
        import markdown  # noqa: F401  — import once here, not inside the first request
    except ImportError:
        return 0
    count = 0
    site = _SiteIndex.build(root)
    for page in site_pages(root):
        if page.suffix != ".md":
            continue
        out = page.with_suffix(GENERATED_SUFFIX)
        stale = not out.is_file() or os.path.getmtime(os.fspath(out)) < os.path.getmtime(os.fspath(page))
        try:
            if materialize_markdown(page, site=site) is not None and stale:
                count += 1
        except (OSError, RuntimeError, UnicodeDecodeError):
            continue   # an unreadable page is reported when it is opened, not at startup
    return count


def materialize_markdown(md, *, site=None):
    """The HTML page for the Markdown file *md*, generated beside it as
    ``<stem>.generated.html`` when missing or older than the Markdown, and left alone
    when it exists without the generated marker (a hand-written page wins). Returns the
    HTML path, or None when the ``markdown`` package is not installed."""
    out = md.with_suffix(GENERATED_SUFFIX)
    src_name = md.name
    if out.is_file():
        try:
            head = out.read_bytes()[:400].decode("utf-8", "replace")
        except RuntimeError:
            head = ""
        if "generated from" not in head or "oo docs serve" not in head:
            return out                                   # not ours: never overwritten
        if RENDERER in head and os.path.getmtime(os.fspath(out)) >= os.path.getmtime(os.fspath(md)):
            return out                                   # fresh, and made by this renderer
    page = (site.pages.get(os.fspath(md)) or site.add(md)) if site is not None else None
    rendered = render_markdown(_page_text(md), md.stem, site=site, page=page)
    if rendered is None:
        return None
    mark = GENERATED_MARK.format(src=src_name)
    out.write_bytes(rendered.replace("<!doctype html>", "<!doctype html>\n" + mark, 1).encode("utf-8"))
    return out


def _relative(root, p) -> str:
    """*p* as the root-relative URL path (forward slashes, leading slash)."""
    rel = os.fspath(p)[len(os.fspath(root)):].replace("\\", "/")
    return "/" + rel.lstrip("/")


def _page_key(root, page: str | None):
    """A ``?page=`` value (a URL path) as a path over *root*'s table, so
    ``/a/./b.html`` and ``/a/b.html`` are the same page; None stays None."""
    if page is None:
        return None
    return root / page.lstrip("/")


def history_of(path) -> dict:
    """When *path* last changed on disk, and what git says of it: its last commit (short id and
    time) and whether the file differs from it — an untracked or ignored file counts as not
    committed. The git fields are None outside a checkout, or where git cannot be run."""
    import subprocess

    changed = datetime.datetime.fromtimestamp(os.path.getmtime(os.fspath(path))).astimezone()
    out = {"changed": changed.isoformat(timespec="seconds"), "commit": None, "committed": None,
           "uncommitted": None}

    def git(*args):
        return subprocess.run(["git", "-C", os.fspath(path.parent), *args, "--", path.name],
                              capture_output=True, text=True, timeout=5, check=False)

    try:
        status = git("status", "--porcelain", "--ignored")
        if status.returncode != 0:
            return out                                   # not a checkout
        log = git("log", "-1", "--format=%h%x09%cI")
    except (OSError, subprocess.TimeoutExpired):
        return out
    out["uncommitted"] = bool(status.stdout.strip())
    if log.returncode == 0 and log.stdout.strip():
        out["commit"], out["committed"] = log.stdout.strip().split("\t", 1)
    return out


def _source_of(root, page: str | None):
    """What the served *page* is made from — the Markdown behind a generated page, or the page
    itself — or None when *page* names nothing under *root* that is a page."""
    if not page:
        return None
    target = (root / page.lstrip("/")).resolve()
    if root not in target.parents or not target.is_file():
        return None
    if target.name.endswith(GENERATED_SUFFIX):
        md = target.parent / (target.name[: -len(GENERATED_SUFFIX)] + ".md")
        return md if md.is_file() else None
    return target if target.suffix.lower() in PAGE_SUFFIXES else None


def _read_mark(root, source):
    """Where *source*'s Markdown is kept as it was when its reader marked it read."""
    return root / READ_REL / (_relative(root, source).lstrip("/") + ".json")


# Each exact likeness computed, by the digests of its two blocks. A page's load, each click that marks
# one change read and the state that click answers with all compare the same pairs of blocks again,
# and one pair of long blocks costs a tenth of a second or more (Debith, 2026-09-27: "Marking
# something read takes several seconds").
_LIKENESS: dict = {}
LIKENESS_KEPT = 100_000


def _likeness(pair, old: str, new: str, score: float):
    """How alike *old* and *new* are — difflib's ratio — when it could reach ``SAME_BLOCK`` and beat
    *score*, else None. *pair* is a SequenceMatcher already holding *new* as its second sequence.
    The cheap upper bounds decide first, so an exact ratio is computed only where it could win, and
    once per pair of blocks."""
    key = (_digest(old), _digest(new))
    ratio = _LIKENESS.get(key)
    if ratio is not None:
        return ratio if ratio >= SAME_BLOCK else None
    pair.set_seq1(old)
    for bound in (pair.real_quick_ratio, pair.quick_ratio):
        upper = bound()
        if upper < SAME_BLOCK or upper <= score:
            return None
    ratio = pair.ratio()
    if len(_LIKENESS) >= LIKENESS_KEPT:
        _LIKENESS.pop(next(iter(_LIKENESS)))
    _LIKENESS[key] = ratio
    return ratio if ratio >= SAME_BLOCK else None


def _alignment(before: list, now: list) -> list:
    """*before* and *now* (lists of blocks) paired in document order: ``(kind, i, j)`` with kind
    ``same``, ``changed`` (old block i became new block j), ``added`` (i None) or ``removed``
    (j None). A replaced range can hold a reworded block beside a deleted one, so each new block
    in it is paired with the old block most like it, in order; what pairs with nothing is added or
    removed."""
    out = []
    matcher = difflib.SequenceMatcher(None, [_digest(b) for b in before], [_digest(b) for b in now], autojunk=False)
    for op, i1, i2, j1, j2 in matcher.get_opcodes():
        if op == "equal":
            out += [("same", i1 + k, j1 + k) for k in range(i2 - i1)]
        elif op == "insert":
            out += [("added", None, j) for j in range(j1, j2)]
        elif op == "delete":
            out += [("removed", i, None) for i in range(i1, i2)]
        else:
            last = i1 - 1
            for j in range(j1, j2):
                best, score = None, 0.0
                pair = difflib.SequenceMatcher(None, autojunk=False)
                pair.set_seq2(now[j])
                for i in range(last + 1, i2):
                    ratio = _likeness(pair, before[i], now[j], score)
                    if ratio is not None and ratio > score:
                        best, score = i, ratio
                if best is None:
                    out.append(("added", None, j))
                    continue
                out += [("removed", i, None) for i in range(last + 1, best)]
                out.append(("changed", best, j))
                last = best
            out += [("removed", i, None) for i in range(last + 1, i2)]
    return out


def _changes(before_text: str, now_text: str) -> dict:
    """What *now_text* adds, changes and removes against *before_text*, block by block: added and
    changed blocks by their token in the page, a changed one with what it said before, and each
    removed block as it read — both rendered."""
    before, now = blocks_of(before_text), blocks_of(now_text)
    tokens = _tokens(now)
    added, changed, removed, last = [], [], [], None
    for kind, i, j in _alignment(before, now):
        if kind == "added":
            added.append(tokens[j])
        elif kind == "changed":
            changed.append({"block": tokens[j], "before": _render_fragment(before[i])})
        elif kind == "removed":
            # shown where it stood: after the page's block it followed (None: at the top)
            removed.append({"block": _removed_token(i, before[i]), "before": _render_fragment(before[i]),
                            "after": last})
        if j is not None:
            last = tokens[j]
    return {"added": added, "changed": changed, "removed": removed}


def _removed_token(i: int, block: str) -> str:
    """What names a removed block: ``r<its place in the old text>-<digest>``, never a page token."""
    return f"r{i}-{_digest(block)}"


def _read_one(before_text: str, now_text: str, token: str) -> str:
    """*before_text* with only the change *token* names taken in: the baseline once that one change
    is read, every other change still new against it. ValueError when *token* names no change."""
    before, now = blocks_of(before_text), blocks_of(now_text)
    tokens = _tokens(now)
    kept, found = [], False
    for kind, i, j in _alignment(before, now):
        if kind in ("added", "changed") and tokens[j] == token:
            kept.append(now[j])
            found = True
        elif kind == "removed" and _removed_token(i, before[i]) == token:
            found = True                                 # read: it is gone from what was read, too
        elif kind != "added":
            kept.append(before[i])
    if not found:
        raise ValueError(f"{token}: not a change on this page")
    return "\n\n".join(kept) + "\n"


def _page_text(page) -> str:
    """A Markdown page's text, its line endings read as \\n. A checkout made with git's autocrlf holds
    \\r\\n on disk and \\n in the commit, and a line ending alone is not a change to its reader."""
    return page.read_bytes().decode("utf-8").replace("\r\n", "\n")


def _committed_text(path):
    """*path*'s text in the last commit: ``""`` when the last commit does not have it — a page never
    committed is new all through — and None outside a checkout, or where git cannot be run."""
    import subprocess

    folder = os.fspath(path.parent)
    try:
        inside = subprocess.run(["git", "-C", folder, "rev-parse", "--is-inside-work-tree"],
                                capture_output=True, timeout=5, check=False)
        if inside.returncode != 0:
            return None
        shown = subprocess.run(["git", "-C", folder, "show", f"HEAD:./{path.name}"],
                               capture_output=True, timeout=5, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return shown.stdout.decode("utf-8", "replace").replace("\r\n", "\n") if shown.returncode == 0 else ""


def read_state(root, page: str | None):
    """What the reader of *page* needs to know: when it last changed, and — for a page made from
    Markdown — what is new to them. That is what changed since they marked it read
    (``baseline: "read"``) or, until they first do, what differs from the page's last commit
    (``baseline: "commit"``, ``marked`` None). ``read`` is None for a page never marked outside a
    checkout, and False for a page with no Markdown behind it. None when *page* is not a page
    under *root*."""
    source = _source_of(root, page)
    if source is None:
        return None
    state = {"page": page, "source": _relative(root, source), "modified": history_of(source)}
    if source.suffix.lower() != ".md":
        state["read"] = False
        return state
    marked, baseline, text = _baseline(root, source)
    state["read"] = None if text is None else {"marked": marked, "baseline": baseline,
                                               **_changes(text, _page_text(source))}
    return state


def _baseline(root, source):
    """What *source* is compared with: ``(marked, "read", text)`` from its read mark, else
    ``(None, "commit", text)`` from its last commit, else ``(None, None, None)``."""
    mark = _read_mark(root, source)
    if mark.is_file():
        kept = mark.read()
        return kept.get("marked"), "read", kept.get("text", "")
    committed = _committed_text(source)
    return (None, "commit", committed) if committed is not None else (None, None, None)


def mark_read(root, page: str | None, block: str | None = None):
    """Keep *page*'s Markdown as it is now, as what its reader has read — or, given *block*, take in
    only that one change and leave the others new; returns the new state. None when *page* is not a
    page under *root*; ValueError for a page with no Markdown behind it, or a *block* that is not a
    change on it."""
    source = _source_of(root, page)
    if source is None:
        return None
    if source.suffix.lower() != ".md":
        raise ValueError(f"{page}: only a page made from Markdown can be marked read")
    now = _page_text(source)
    if block:
        _, _, text = _baseline(root, source)
        if text is None:
            raise ValueError(f"{page}: nothing to compare with, so no single change to mark")
        now = _read_one(text, now, block)
    mark = _read_mark(root, source)
    mark.parent.mkdir(parents=True, exist_ok=True)
    mark.write({"page": page, "marked": _now(), "text": now})
    return read_state(root, page)


LISTING_STYLE = """<style>
.ls{list-style:none;padding:0;margin:1rem 0}
.ls li{display:flex;gap:.6rem;align-items:baseline;padding:.25rem 0;border-bottom:1px solid #eceae4}
.ls a{color:#1d242b;text-decoration:none}.ls a:hover,.ls a:focus-visible{text-decoration:underline}
.ls li.dir a{font-weight:600}
.tag{font-size:.72rem;line-height:1.5;padding:0 .5rem;border-radius:9px;white-space:nowrap}
.tag.read{background:rgba(176,110,20,.13);color:#80500c}
.tag.git{background:#eceae4;color:#4f5961}
.legend{color:#5f6a72;font-size:.85rem}
</style>"""


def _changed_since_read(root) -> set:
    """Every page under *root* whose Markdown differs from the text kept when it was marked read,
    as absolute path strings."""
    notes = root / READ_REL
    if not notes.is_dir():
        return set()
    base, changed = os.fspath(notes), set()
    for mark in notes.pathset("**/*.json"):
        source = root / os.fspath(mark)[len(base) + 1: -len(".json")]
        try:
            if source.is_file() and _page_text(source) != mark.read().get("text"):
                changed.add(os.fspath(source))
        except (OSError, RuntimeError, UnicodeDecodeError, AttributeError):
            continue                                     # an unreadable mark marks nothing
    return changed


def _uncommitted(root) -> set:
    """Every file under *root* git has not committed — modified, added or untracked — as absolute
    path strings. Ignored files are left out, as they are meant to be; empty outside a checkout."""
    import subprocess

    def git(*args):
        return subprocess.run(["git", "-C", os.fspath(root), *args], capture_output=True, text=True,
                              timeout=10, check=False)

    try:
        top = git("rev-parse", "--show-toplevel")
        if top.returncode != 0:
            return set()
        status = git("status", "--porcelain=v1", "-z", "--untracked-files=all")
    except (OSError, subprocess.TimeoutExpired):
        return set()
    base, out, fields, i = top.stdout.strip(), set(), status.stdout.split("\0"), 0
    while i < len(fields):
        entry, i = fields[i], i + 1
        if len(entry) < 4:
            continue
        if entry[0] in "RC":
            i += 1                                       # the path it was renamed or copied from
        out.add(os.path.normpath(os.path.join(base, entry[3:])))
    return out


def _listing(root, folder, url_path: str) -> str:
    """*folder* as a page of its entries, folders first: a file is marked when it changed since it
    was marked read, or git has not committed it; a folder gives how many such files it holds at
    any depth. A generated page is never marked — its Markdown carries the mark."""
    read_changed, uncommitted = _changed_since_read(root), _uncommitted(root)
    uncommitted = {p for p in uncommitted if not p.endswith(GENERATED_SUFFIX)}

    def tags(read, git):
        """A file's marks (``True``) or a folder's counts (numbers)."""
        def tag(kind, value, text):
            if not value:
                return ""
            count = "" if value is True else f"{value} "
            return f'<span class="tag {kind}">{count}{text}</span>'
        return tag("read", read, "changed since read") + tag("git", git, "not committed")

    rows = [] if folder == root else ['<li class="dir"><a href="../">../</a></li>']
    for entry in sorted(folder.pathset("*"), key=lambda p: (not p.is_dir(), p.name.lower())):
        path, name = os.fspath(entry), entry.name
        if entry.is_dir():
            inside = path + os.sep
            marks = tags(sum(p.startswith(inside) for p in read_changed), sum(p.startswith(inside) for p in uncommitted))
            rows.append(f'<li class="dir"><a href="{urllib.parse.quote(name)}/">{html.escape(name)}/</a>{marks}</li>')
        else:
            marks = "" if name.endswith(GENERATED_SUFFIX) else tags(path in read_changed, path in uncommitted)
            rows.append(f'<li><a href="{urllib.parse.quote(name)}">{html.escape(name)}</a>{marks}</li>')
    title = html.escape(url_path)
    legend = (f'<p class="legend">{tags(True, False)} the page changed after it was marked read &nbsp; '
              f'{tags(False, True)} git has not committed it — a folder counts both below it</p>')
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>{title}</title>{MARKDOWN_STYLE}{LISTING_STYLE}'
            f'</head><body><h1>{title}</h1>{legend}<ul class="ls">{"".join(rows)}</ul></body></html>')


def _pick_index(root, index: str | None) -> str | None:
    """The root-relative page ``/`` redirects to, or None to serve the root as-is."""
    if index:
        return index.replace("\\", "/").lstrip("/")
    if any((root / c).is_file() for c in ("index.html", "README.md", "index.md")):
        return None
    return next((c for c in INDEX_CANDIDATES if (root / c).is_file()), None)


def _make_handler(root, index: str | None, site=None):
    """A SimpleHTTPRequestHandler bound to *root* with the comment, pages and upload endpoints.
    *root* carries the server's store; every path made here derives from it."""
    store = root.store
    comments = root / COMMENTS_REL  # a pathlike jsonlpath: read() -> list, write(list)

    def load_comments() -> list:
        return comments.read() if comments.is_file() else []

    def save_comments(rows: list) -> None:
        comments.parent.mkdir(parents=True, exist_ok=True)
        comments.write(rows)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(root), **kw)

        # ---- GET -------------------------------------------------------
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            route = parsed.path.rstrip("/")
            if route == "/comments":
                wanted = _page_key(root, urllib.parse.parse_qs(parsed.query).get("page", [None])[0])
                self._guarded(lambda: self._send_json([
                    c for c in load_comments()
                    if c.get("status", "open") == "open"
                    and (wanted is None or _page_key(root, c.get("page")) == wanted)]))
                return
            if route == "/alive":
                self._send_json({"boot": BOOT})         # a new value is a new run: open pages reload
                return
            if route == "/pages":
                self._send_json(sorted(_relative(root, p) for p in listed_pages(root)))
                return
            if route == "/read-state":
                def answer():
                    state = read_state(root, urllib.parse.parse_qs(parsed.query).get("page", [None])[0])
                    if state is None:
                        self.send_error(404, "not a page here")
                    else:
                        self._send_json(state)
                self._guarded(answer)
                return
            if self.path in ("/", "/index.html") and index and not (root / "index.html").is_file() \
                    and not (root / "README.md").is_file() and not (root / "index.md").is_file():
                self.send_response(302)
                self.send_header("Location", "/" + index)
                self.end_headers()
                return
            # a Markdown page is generated as HTML beside its source and served from there
            page = pygim.path(self.translate_path(parsed.path), store=store)   # a row of the server's table
            if page.is_dir():
                page = next((page / c for c in ("index.html", "README.md", "index.md") if (page / c).is_file()),
                            page / "index.html")
            if page.suffix.lower() == ".md" and page.is_file():
                generated = materialize_markdown(page, site=site)
                if generated is not None:
                    self.send_response(302)
                    self.send_header("Location", urllib.parse.quote(_relative(root, generated)))
                    self.end_headers()
                    return
            # a generated page is brought up to date with its Markdown before it is served
            if page.name.endswith(GENERATED_SUFFIX):
                md = page.parent / (page.name[: -len(GENERATED_SUFFIX)] + ".md")
                if md.is_file():
                    materialize_markdown(md, site=site)
            # every served HTML page gets the review layer: the ✎ commenter and what changed
            if page.suffix.lower() == ".html" and page.is_file():
                try:
                    text = page.read_bytes().decode("utf-8")
                except (RuntimeError, UnicodeDecodeError):
                    super().do_GET()
                    return
                body = _commenter.inject(text).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            super().do_GET()

        def list_directory(self, path):
            """A folder with no index page, listed with what changed in it (see ``_listing``)."""
            folder = pygim.path(path, store=store).resolve()
            try:
                page = _listing(root, folder, urllib.parse.unquote(urllib.parse.urlparse(self.path).path))
            except OSError:
                self.send_error(404, "No permission to list directory")
                return None
            body = _commenter.inject(page).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            return io.BytesIO(body)

        # ---- helpers ---------------------------------------------------
        def _guarded(self, action):
            """Run *action*; a comments-file decode error becomes a 500 that names the file and line."""
            try:
                action()
            except RuntimeError as exc:
                self.send_error(500, str(exc))

        def _send_json(self, obj, status=200):
            body = json.dumps(obj).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _body_length(self, limit):
            try:
                length = int(self.headers.get("Content-Length", 0))
            except ValueError:
                length = 0
            return length if 0 < length <= limit else None

        def _read_body(self, length):
            self._body_read = True
            return self.rfile.read(length)

        def _discard_body(self):
            """Read and drop a request body nobody consumed, so the client sees our reply.

            Windows resets a connection the server closes with bytes still unread, and
            the client then reports WinError 10053 instead of our status line. Bodies
            beyond the upload limit are not drained; the reset is the cheaper answer.
            """
            if getattr(self, "_body_read", True):
                return
            try:
                left = int(self.headers.get("Content-Length", 0))
            except ValueError:
                left = 0
            if left > MAX_BYTES:
                return
            while left > 0:
                chunk = self.rfile.read(min(left, 65536))
                if not chunk:
                    break
                left -= len(chunk)
            self._body_read = True

        def send_error(self, code, message=None, explain=None):
            self._discard_body()
            super().send_error(code, message, explain)

        def _read_json_body(self, limit=MAX_COMMENT):
            length = self._body_length(limit)
            if length is None:
                return None
            try:
                obj = json.loads(self._read_body(length).decode("utf-8"))
            except (ValueError, UnicodeDecodeError):
                return None
            return obj if isinstance(obj, dict) else None

        # ---- POST ------------------------------------------------------
        def do_POST(self):
            self._body_read = False
            parsed = urllib.parse.urlparse(self.path)
            route = parsed.path.rstrip("/")
            if route == "/comment":
                self._guarded(self._post_comment)
            elif route in ("/comment-edit", "/comment-delete"):
                self._guarded(lambda: self._post_comment_change(editing=route == "/comment-edit"))
            elif route == "/upload":
                self._post_upload(parsed)
            elif route == "/read":
                self._guarded(self._post_read)
            else:
                self.send_error(404, "not found")

        def _post_read(self):
            obj = self._read_json_body()
            try:
                state = mark_read(root, obj.get("page") if obj else None, obj.get("block") if obj else None)
            except ValueError as exc:
                self.send_error(400, str(exc))
                return
            if state is None:
                self.send_error(404, "not a page here")
                return
            print(f"  marked read: {state['source']}")
            self._send_json(state)

        def _post_comment(self):
            if self._body_length(MAX_COMMENT) is None:
                self.send_error(413, "bad size")
                return
            obj = self._read_json_body()
            if obj is None or not str(obj.get("text", "")).strip():
                self.send_error(400, "bad comment")
                return
            obj["stored"] = _now()
            obj.setdefault("id", obj["stored"] + "-" + str(os.getpid() % 1000))
            obj.setdefault("status", "open")
            save_comments(load_comments() + [obj])
            print(f"  comment on {obj.get('page', '?')}: {str(obj['text'])[:60]}")
            self._send_json(obj)

        def _post_comment_change(self, *, editing: bool):
            obj = self._read_json_body()
            if obj is None or not obj.get("id"):
                self.send_error(400, "bad request")
                return
            if editing and not str(obj.get("text", "")).strip():
                self.send_error(400, "empty text")
                return
            rows = load_comments()
            for c in rows:
                if c.get("id") == obj["id"] and editing:
                    c["text"], c["edited"] = obj["text"], _now()
            save_comments(rows if editing else [c for c in rows if c.get("id") != obj["id"]])
            self._send_json({"ok": True})

        def _post_upload(self, parsed):
            target = _upload_target(root, urllib.parse.parse_qs(parsed.query))
            if not target:
                self.send_error(400, "bad path")
                return
            dest, rel = target
            length = self._body_length(MAX_BYTES)
            if length is None:
                self.send_error(413, "bad size")
                return
            data = self._read_body(length)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            print(f"  wrote {rel}  ({len(data)} bytes)")
            self._send_json({"ok": True, "path": rel})

        def log_message(self, fmt, *args):
            pass  # quiet the per-request GET spam; POSTs print their own line

    return Handler


def _lan_ip():
    """Best-effort LAN IP (the WSL address), or None."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
    except OSError:
        return None
    return ip


def rebuild(doc_root, command: str, *, store=None):
    """Run *command* in *doc_root* and report what it changed: the root-relative
    URLs of pages added and pages removed, as two sorted lists (a set
    difference over the site's pages before and after). Raises
    :class:`ServeError` with the exit code when the command fails."""
    import subprocess

    root = pygim.path(doc_root, store=store or PathStore()).resolve()
    before = site_pages(root)
    rc = subprocess.run(command, shell=True, cwd=os.fspath(root), check=False).returncode
    if rc:
        raise ServeError(f"rebuild command failed (exit {rc}): {command}")
    after = site_pages(root)
    added = sorted(_relative(root, p) for p in after - before)
    removed = sorted(_relative(root, p) for p in before - after)
    return added, removed


def make_server(doc_root, *, port: int = 8000, host: str | None = None,
                index: str | None = None, store=None) -> socketserver.TCPServer:
    """Build (and bind) the server for *doc_root* without running it.

    *store* is the PathStore every path the server makes lives in (default: a
    fresh one, so the module's default store never sees request traffic).
    Raises :class:`ServeError` if it cannot bind, and ``FileNotFoundError`` if
    *doc_root* is not a directory. ``port=0`` picks a free port (tests)."""
    root = pygim.path(doc_root, store=store or PathStore()).resolve()
    if not root.is_dir():
        raise FileNotFoundError(f"doc root not found: {root}")
    pregenerate(root)   # every Markdown page has its HTML before the first request
    site = _SiteIndex.build(root)

    # Bind all interfaces by default so the page is reachable via the WSL IP even
    # when Windows→WSL localhost forwarding hiccups (a common "can't connect").
    # NB: PYGIM_HOST, not HOST — interactive shells often set $HOST to the machine
    # name, which would try to bind an unresolvable hostname (Errno -3).
    host = host or os.environ.get(HOST_ENV, "0.0.0.0")
    # POSIX: reuse the address so a restart does not wait out TIME_WAIT. Windows:
    # SO_REUSEADDR there lets a SECOND server bind the same port (no error, split
    # traffic), so leave it off — Windows does not have the TIME_WAIT bind problem.
    socketserver.TCPServer.allow_reuse_address = os.name != "nt"
    handler = _make_handler(root, _pick_index(root, index), site)
    try:
        return socketserver.TCPServer((host, port), handler)
    except OSError as e:
        lines = [f"Could NOT start on {host}:{port} — {e}", ""]
        if getattr(e, "errno", None) == errno.EADDRINUSE:
            lines += [
                "That port is already in use. Either:",
                "  • stop the old server:  pkill -f 'oo docs serve'   (then run this again), or",
                f"  • use another port:     oo docs serve --port {port + 1}",
            ]
        else:
            lines += [
                f"Binding to host {host!r} failed (a name-resolution / interface error, not the port).",
                "Force the interface explicitly:",
                f"  oo docs serve --host 0.0.0.0 --port {port}      # all interfaces (localhost + LAN IP)",
                f"  oo docs serve --host 127.0.0.1 --port {port}    # localhost only",
            ]
        raise ServeError("\n".join(lines)) from e


class Watch:
    """Files, and whether any has changed since this last looked."""

    def __init__(self, files):
        self.files = list(files)
        self.seen = self._stamps()

    def _stamps(self) -> dict:
        stamps = {}
        for f in self.files:
            try:
                stamps[os.fspath(f)] = os.path.getmtime(os.fspath(f))
            except OSError:
                stamps[os.fspath(f)] = None               # gone, or not yet there: that is a state too
        return stamps

    def changed(self) -> bool:
        now = self._stamps()
        moved, self.seen = now != self.seen, now
        return moved


def own_sources() -> list:
    """The code a running server has loaded that changes while it runs: this package, the review
    layer's fragment beside it, and the command line that starts it."""
    here = pygim.path(os.path.dirname(os.path.abspath(__file__)))
    return sorted(here.pathset("*.py"), key=os.fspath) + [here.parent.parent / "pygim" / "__main__.py"]


def _reexec() -> None:
    """Start this same command again in this same process: new code, same terminal, same port."""
    import sys
    os.execv(sys.executable, [sys.executable, *sys.argv])


def serve(doc_root, *, port: int = 8000, host: str | None = None, index: str | None = None, store=None,
          reload: bool = True, watch=None, restart=None, interval: float = 1.0) -> None:
    """Serve *doc_root* on *port* until Ctrl-C. See :func:`make_server` for errors.

    With *reload* the server watches its own code (*watch*, default `own_sources`) and, when a file
    changes, stops and starts again through *restart* (default: the same command, in place). Open
    pages see the new run's `BOOT` at ``/alive`` and reload themselves."""
    import threading

    httpd = make_server(doc_root, port=port, host=host, index=index, store=store)
    stopped, changed = threading.Event(), threading.Event()
    if reload:
        watched = Watch(own_sources() if watch is None else watch)

        def look():
            while not stopped.wait(interval):
                if watched.changed():
                    changed.set()
                    httpd.shutdown()
                    return

        threading.Thread(target=look, daemon=True).start()
    port = httpd.server_address[1]
    ip = _lan_ip()
    print("Docs are UP. Open EITHER url in your browser:")
    print(f"    http://localhost:{port}/")
    if ip:
        print(f"    http://{ip}:{port}/      (use this one if localhost won't connect)")
    root = pygim.path(doc_root, store=store or PathStore()).resolve()
    print(f"\nServing {root}  ({len(site_pages(root))} pages; GET /pages lists them)")
    print(f"Comments (✎) land in {COMMENTS_REL}; pages marked read keep their text in {READ_REL}/;")
    print("image drops write under images/. Ctrl-C to stop.")
    print("(Bound on all interfaces for WSL reachability — it's LAN-visible while running.)\n")
    if reload:
        print("Restarts itself when its code changes; open pages reload with it (--no-reload: off).\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        stopped.set()
        httpd.server_close()
    if changed.is_set():
        print("\ncode changed — restarting.\n", flush=True)
        (restart or _reexec)()
