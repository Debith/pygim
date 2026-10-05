"""What must hold for every markdown document, checked over generated ones.

Not a test module (pytest does not collect it): test_pathlike_markdown_adversarial.py
runs `main()` in a child process — a crash there is the process dying, so the child
prints each case before checking it — and imports the generators for its own tests.

Two sources of input:
- random documents from a token alphabet chosen to start every construct: every
  emphasis and fence character, link and image openers, raw HTML and comments,
  entities (valid, out of range, NUL), every list marker, quotes, tables, front
  matter fences, definitions, task markers, every line ending, tabs, NUL,
  combining marks and bidi controls;
- the CommonMark spec's own examples, mutated by small edits, so real structure
  is broken in realistic ways.

`check(text)` returns what it found violated, as short strings; empty is good.
"""

import json
import pickle
import random
import re
import sys
from html.parser import HTMLParser

from pygim import pathlike

md = pathlike.markdown

TOKENS = [
    "*", "**", "_", "__", "~", "~~", "`", "``", "```", "~~~", "[", "]", "(", ")", "![", "<", ">", "</", "<!--",
    "-->", "<?", "?>", "<![CDATA[", "]]>", "<div>", "</div>", "<a href=\"x\">", "</a>", "<span title='t'>",
    "&amp;", "&#0;", "&#x110000;", "&#99999999;", "&nbsp;", "&bogus;", "&", "\\", "#", "## ", "###### x",
    "####### x", "- ", "+ ", "* ", "1. ", "1) ", "999999999. ", "> ", ">", "|", "| a | b |\n|---|:-:|\n",
    "| x |", ":", "-", "---", "***", "___", "===", "+++", "...", "[x]: /u \"t\"\n", "[x]", "[X]: <a b>\n",
    "[ ]", "[x] ", "http://a.b", "www.x.y", "<http://a>", "<a@b.c>", "\n", "\n\n", "\r\n", "\r", "\t", "    ",
    " ", "  ", "a", "word", "é", "日本", "🚀", "́", "‮", "​", "\x00", "\x1c", "'", "\"", "=",
    "*a*", "_b_", "`code`", "[link](/u)", "[l](</a b> 't')", "![i](/i.png)", "\\*", "\\\n", "Σ", "İ",
]
# Without raw HTML: whatever tags the output holds, the renderer wrote — so these documents
# test that text, links and attributes can never become markup of their own.
TEXT_TOKENS = [t for t in TOKENS if "<" not in t]
# Where the dialects may differ: tables, strikethrough, task items. A table needs no pipe at all —
# cmark-gfm's delimiter row is `[|]? marker ([|] marker)* [|]?` — so a line like `:---` counts too.
GFM_SYNTAX = re.compile(r"[|~]|\[[ xX]\]|^[ \t]*:?-+:?[ \t]*$", re.MULTILINE)


def random_document(rng, most=60, tokens=TOKENS):
    return "".join(rng.choice(tokens) for _ in range(rng.randint(0, most)))


def mutate(rng, text, edits=3):
    """`text` with a few random insertions, deletions and repeats."""
    for _ in range(edits):
        if not text:
            text = rng.choice(TOKENS)
            continue
        i = rng.randrange(len(text) + 1)
        op = rng.randrange(3)
        if op == 0:
            text = text[:i] + rng.choice(TOKENS) + text[i:]
        elif op == 1:
            text = text[:i] + text[i + rng.randint(1, 4):]
        else:
            j = min(len(text), i + rng.randint(1, 12))
            text = text[:j] + text[i:j] + text[j:]
    return text


class _Tags(HTMLParser):
    """Every tag and attribute the HTML holds."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.tags, self.attrs = set(), set()

    def handle_starttag(self, tag, attrs):
        self.tags.add(tag)
        self.attrs.update(a for a, _ in attrs)

    handle_startendtag = handle_starttag


# What the renderer itself writes. Anything else in the HTML came from raw HTML in the input.
RENDERER_TAGS = {"p", "h1", "h2", "h3", "h4", "h5", "h6", "em", "strong", "del", "code", "pre", "a", "img", "ul",
                 "ol", "li", "blockquote", "hr", "br", "table", "thead", "tbody", "tr", "th", "td", "input"}
RENDERER_ATTRS = {"href", "title", "src", "alt", "class", "start", "align", "type", "checked", "disabled"}


def _lines(text):
    """The lines as markdown ends them — LF, CRLF or a lone CR — each with its ending
    (str.splitlines also splits at \\x1c, \\x85, \\u2028 and more)."""
    return re.findall(r"[^\r\n]*(?:\r\n|\r|\n)|[^\r\n]+$", text)


def _shape(doc):
    return [(type(b).__name__, b.lines, b.plain) for b in doc.walk()]


def check(text):
    """The invariants `text` breaks, as short strings."""
    bad = []

    def expect(ok, what):
        if not ok:
            bad.append(what)

    for dialect in ("gfm", "commonmark"):
        doc = md.Document(text, dialect=dialect)
        tag = f"[{dialect}] "
        expect(doc.text == text, tag + "the text is not the input")
        lines = _lines(text)
        walk = doc.walk()
        for b in walk:
            a, z = b.span
            expect(doc.text[a:z] == b.text, tag + f"{type(b).__name__} {b.lines}: span does not slice its text")
            first, last = b.lines
            expect(1 <= first <= last and b.text == "".join(lines[first - 1:last]),
                   tag + f"{type(b).__name__} {b.lines}: lines do not hold its text")
            parent = b.parent
            if parent is not None:
                pa, pz = parent.span
                expect(pa <= a and z <= pz, tag + f"{type(b).__name__} {b.lines}: outside its parent")
        for container in [None] + walk:
            kids = doc.blocks if container is None else container.children
            for x, y in zip(kids, kids[1:]):
                expect(x.span[1] <= y.span[0], tag + f"{type(x).__name__} {x.lines} overlaps the next block")
        preorder = []
        todo = list(reversed(doc.blocks))
        while todo:
            b = todo.pop()
            preorder.append(b)
            todo.extend(reversed(b.children))
        expect(preorder == walk, tag + "walk() is not depth-first over children")
        for cls in {type(b) for b in walk}:
            expect(doc.find(cls) == [b for b in walk if type(b) is cls], tag + f"find({cls.__name__}) disagrees with walk()")
        for b in doc.blocks:
            expect(doc.replace(b, b.text).text == text, tag + f"replacing {type(b).__name__} {b.lines} by itself changed the text")
        sections = doc.sections
        for s in sections:
            expect(doc.replace(s, s.text).text == text, tag + f"replacing section {s.slug!r} by itself changed the text")
            if s.parent is not None:
                expect(s.parent.span[0] <= s.span[0] and s.span[1] <= s.parent.span[1], tag + f"section {s.slug!r} outside its parent")
        for x, y in zip(sections, sections[1:]):
            expect(x.span[0] < y.span[0], tag + "sections out of order")
        for slugs in ("github", "toc"):
            names = [h.slug for h in md.Document(text, dialect=dialect, slugs=slugs).find(md.Heading)]
            expect(len(names) == len(set(names)), tag + f"{slugs} slugs repeat: {names}")
        expect(_shape(md.Document(text, dialect=dialect)) == _shape(doc), tag + "two parses differ")
        html, plain = doc.html(), doc.plain
        expect(html == "".join(b.html() for b in doc.blocks), tag + "html() is not its blocks' html() in order")
        expect(plain == "\n\n".join(b.plain for b in doc.blocks if b.plain), tag + "plain is not its blocks' plain")
        expect("\x00" not in html and "\x00" not in plain, tag + "a NUL reached the output")
        if "<" not in text:
            seen = _Tags()
            seen.feed(html)
            expect(seen.tags <= RENDERER_TAGS, tag + f"tags the renderer does not write: {seen.tags - RENDERER_TAGS}")
            expect(seen.attrs <= RENDERER_ATTRS, tag + f"attributes the renderer does not write: {seen.attrs - RENDERER_ATTRS}")
        back = pickle.loads(pickle.dumps(doc))
        expect((back.text, back.dialect) == (text, dialect), tag + "a pickle does not come back")
        stats = doc.stats()
        expect(stats["source"] == len(text.encode("utf-8", "surrogatepass")), tag + "stats()['source'] is not the size")
        expect(stats["bytes"] <= 400 * max(1, stats["source"]) + 65536, tag + f"{stats['bytes']} bytes for {stats['source']}")
    if "\r" not in text:
        crlf = md.Document(text.replace("\n", "\r\n"))
        expect(_shape(crlf) == _shape(md.Document(text)), "CRLF reads differently from LF")
    if not GFM_SYNTAX.search(text):
        expect(md.Document(text).html() == md.Document(text, dialect="commonmark").html(),
               "the dialects differ without GFM syntax")
    return bad


def cases(seed, count, spec):
    """`count` random documents (half without raw HTML), then each spec example mutated once, all from `seed`."""
    rng = random.Random(seed)
    for i in range(count):
        yield random_document(rng, tokens=TOKENS if i % 2 else TEXT_TOKENS)
    for example in spec:
        yield mutate(rng, example["markdown"])


def main():
    """Run in a child process: config on stdin, a line before each case, violations as JSON."""
    config = json.load(sys.stdin)
    spec = pathlike.path(config["spec"]).read() if config.get("spec") else []
    for i, text in enumerate(cases(config["seed"], config["count"], spec)):
        print(json.dumps(["case", i, text]), flush=True)
        for what in check(text):
            print(json.dumps(["bad", i, what]), flush=True)
    print(json.dumps(["done"]), flush=True)
