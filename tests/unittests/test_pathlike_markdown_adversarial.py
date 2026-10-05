"""pathlike.markdown against generated input: invariants, round trips, threads, injection.

Where test_pathlike_markdown.py pins chosen cases and the hostile tests pin
inputs that once broke it, these generate input to find what nobody chose:

- every invariant of a parse (tests/unittests/_markdown_invariants.py), over
  random documents and mutated spec examples, in a child process — a crash
  there reports its input;
- what the builders and escape() write reads back as what they were given;
- front matter written from random data reads back as that data;
- toc slugs agree with Python-Markdown on random titles;
- one Document read from many threads gives what it gives one;
- a link's destination or title never adds markup to the HTML.

Every generator is seeded, so a failure reproduces. PYGIM_MARKDOWN_FUZZ=N
runs N random documents through the invariants instead of 300 (a deeper run
outside CI: 50,000 take about 15 seconds).
"""

import json
import os
import random
import subprocess
import sys
import threading
from html.parser import HTMLParser

import pytest

import pygim
from pygim import pathlike

md = pathlike.markdown
HERE = pygim.path(__file__).parent


# --------------------------------------------------------------------------- #
# Every invariant, over generated documents, in a child process
# --------------------------------------------------------------------------- #
def test_generated_documents_keep_every_invariant():
    count = int(os.environ.get("PYGIM_MARKDOWN_FUZZ", "300"))
    config = {"seed": 20261005, "count": count, "spec": os.fspath(HERE / "data" / "markdown" / "commonmark-0.31.2.json")}
    script = f"import sys; sys.path.insert(0, {os.fspath(HERE)!r}); import _markdown_invariants as m; m.main()"
    proc = subprocess.run([sys.executable, "-c", script], input=json.dumps(config), capture_output=True, text=True,
                          encoding="utf-8", timeout=1800, env=os.environ.copy())
    events = [json.loads(line) for line in proc.stdout.splitlines() if line.startswith("[")]
    last_case = next((e for e in reversed(events) if e[0] == "case"), None)
    assert proc.returncode == 0 and events[-1:] == [["done"]], (
        f"the child died (rc={proc.returncode}) on case {last_case}\n{proc.stderr[-2000:]}")
    texts = {e[1]: e[2] for e in events if e[0] == "case"}
    bad = [(e[1], e[2]) for e in events if e[0] == "bad"]
    assert not bad, "\n".join(f"{what}\n    input: {texts[i]!r}" for i, what in bad[:5])
    assert len(texts) == count + 652


# --------------------------------------------------------------------------- #
# What is written reads back
# --------------------------------------------------------------------------- #
PLAIN = list("*_[]()!<>#-+=:|~`\\&;.)\"'") + [" ", "  ", "\t", "\r", "\n", "a", "b1", "é", "日", "🚀", "́",
                                              "&amp;", "&#65;", "1.", "10)", "---", "===", "\x1c", " "]


def _plain_texts(seed, n):
    """Plain text a paragraph can hold: no blank line, no line break at either end."""
    rng = random.Random(seed)
    for _ in range(n):
        t = "".join(rng.choice(PLAIN) for _ in range(rng.randint(1, 25))).replace("\r\n", "\n").strip("\n")
        while "\n\n" in t:
            t = t.replace("\n\n", "\n")
        if t:
            yield t


@pytest.mark.parametrize("seed", range(4))
def test_escaped_text_reads_back_exactly(seed):
    for t in _plain_texts(seed, 500):
        doc = md.Document(md.escape(t))
        assert [type(b).__name__ for b in doc.blocks] == ["Paragraph"] and doc.blocks[0].plain == t, repr(t)
        quoted = md.Document(md.quote(md.escape(t))).blocks
        assert [type(b).__name__ for b in quoted] == ["Quote"] and quoted[0].plain == t, repr(t)


def test_builders_read_back_what_they_were_given():
    for t in _plain_texts(9, 1500):
        line = t.replace("\n", " ").replace("\r", " ")
        assert md.Document(md.heading(3, md.escape(line))).find(md.Heading)[0].title == line, repr(line)
        code = t + "\n"
        (block,) = md.Document(md.code(code, "py")).find(md.Code)
        lines = code.replace("\r\n", "\n").replace("\r", "\n")   # CRLF is one line ending, a lone CR another
        assert block.code == lines and block.lang == "py", repr(code)
        (table,) = md.Document(md.table(["h", "g"], [[md.escape(line), "x"]])).find(md.Table)
        assert table.rows == [[line, "x"]], repr(line)
        items = md.Document(md.bullets([md.escape(line), "z"], numbered=True)).find(md.Item)
        assert [i.plain for i in items] == [line, "z"], repr(line)


STRINGS = ["a", "b c", "---", "+++", "...", "x\n---\ny", "q\n+++\nz", '"q"', "it's", "é日🚀", "", " lead", "trail ",
           "#", ": colon", "- dash", "[x]", "{y}", "\t", "yes", "no", "null", "~", "1e3", "0x1F", "2026-10-05",
           "&amp;", "\\", "\r\n", "\r", "bell\x07"]


def _value(rng, depth=0):
    r = rng.random()
    if depth < 3 and r < 0.2:
        return {f"k{i}": _value(rng, depth + 1) for i in range(rng.randint(0, 3))}
    if depth < 3 and r < 0.35:
        return [_value(rng, depth + 1) for _ in range(rng.randint(0, 3))]
    if r < 0.5:
        return rng.randint(-10**12, 10**12)
    if r < 0.6:
        return rng.choice([True, False])
    if r < 0.7:
        return rng.choice([0.5, -2.25, 1e-9, 3.0])
    return "".join(rng.choice(STRINGS) for _ in range(rng.randint(0, 4)))


@pytest.mark.parametrize("engine", ["yaml", "toml"])
def test_front_matter_reads_back_random_data(engine):
    rng = random.Random(engine)
    for _ in range(600):
        data = {f"key{i}": _value(rng) for i in range(rng.randint(1, 4))}
        doc = md.Document("# Body\n").with_front_matter(data, engine=engine)
        assert doc.front_matter == data and doc.text.endswith("# Body\n"), data


def test_a_yaml_string_keeps_its_carriage_returns(temp_dir):
    data = {"crlf": "x\r\ny", "cr": "p\rq", "bell": "b\x07"}   # YAML folds a CR outside double quotes
    assert md.Document("# B\n").with_front_matter(data).front_matter == data
    p = pygim.path(temp_dir) / "cr.yaml"
    p.write(data)
    assert p.read() == data


def test_toc_slugs_agree_with_python_markdown_on_random_titles():
    toc = pytest.importorskip("markdown.extensions.toc")
    rng = random.Random(3)
    parts = STRINGS + ["_1", "_2", "-", "  ", "!", "Ünï", "\x1c", "\x0b", "Straße", "ﬁ"]
    for _ in range(1000):
        titles = ["".join(rng.choice(parts) for _ in range(rng.randint(0, 4))).replace("\n", " ").replace("\r", " ")
                  for _ in range(rng.randint(1, 6))]
        ids = set()
        expected = [toc.unique(toc.slugify(t, "-"), ids) for t in titles]
        doc = md.Document("".join(f"# {md.escape(t)}\n\n" for t in titles), slugs="toc")
        assert [h.slug for h in doc.find(md.Heading)] == expected, titles


# --------------------------------------------------------------------------- #
# Threads
# --------------------------------------------------------------------------- #
def test_one_document_read_from_many_threads_gives_what_one_thread_gets():
    text = (HERE / "data" / "markdown" / "README.md").read_bytes().decode("utf-8") * 20 + "\n# T\n\n## T\n"
    doc = md.Document(text)

    def answers(d):
        return d.html(), d.plain, [s.slug for s in d.sections], [b.span for b in d.walk()]

    expected = answers(md.Document(text))
    wrong = []

    def read():
        for _ in range(10):
            if answers(doc) != expected:
                wrong.append(threading.current_thread().name)

    threads = [threading.Thread(target=read) for _ in range(8)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not wrong   # the caches fill once, under the GIL, whichever thread asks first


# --------------------------------------------------------------------------- #
# Injection
# --------------------------------------------------------------------------- #
class _Tags(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.found = []

    def handle_starttag(self, tag, attrs):
        self.found.append((tag, [name for name, _ in attrs]))


def test_a_link_destination_or_title_never_adds_markup():
    rng = random.Random(5)
    dests = ["a", '"', "'", "&", "javascript:", "%20", '\\"', "\\>", "é", "&quot;", "&#34;", "\x00", "\\\\"]
    titles = ["t", '"', "'", "&", " onclick=x ", '\\"', "&quot;", "&#34;", "\\\\", "\x00"]
    rendered = 0
    for _ in range(1500):
        dest = "".join(rng.choice(dests) for _ in range(rng.randint(1, 8)))
        title = "".join(rng.choice(titles) for _ in range(rng.randint(0, 6)))
        for src in (f'[x](<{dest}> "{title}")', f"[x](<{dest}> '{title}')", f'[x]\n\n[x]: <{dest}> "{title}"\n',
                    f'![x](<{dest}> "{title}")'):
            tags = _Tags()
            tags.feed(md.Document(src).html())
            links = [(t, names) for t, names in tags.found if t in ("a", "img")]
            if not links:
                continue   # the link did not form: the text renders as text
            rendered += 1
            assert {t for t, _ in tags.found} <= {"p", "a", "img"}, src
            for _, names in links:
                assert sorted(names) == sorted(set(names)) and set(names) <= {"href", "title", "src", "alt"}, src
    assert rendered > 3000


def test_html_is_not_a_sanitizer():
    """CommonMark passes raw HTML through and leaves a link's scheme alone, and html() renders
    as its reference renderer does: input from someone untrusted needs a sanitizer after it."""
    assert md.Document("<script>x</script>\n").html() == "<script>x</script>\n"
    assert md.Document("[x](javascript:alert(1))\n").html() == '<p><a href="javascript:alert(1)">x</a></p>\n'


@pytest.mark.parametrize("entity", ["&#0;", "&#x110000;", "&#xD800;", "&#9999999;"])
def test_a_character_reference_to_no_character_is_the_replacement_character(entity):
    assert md.Document(f"a{entity}b\n").html() == "<p>a�b</p>\n"


def test_a_decimal_reference_has_at_most_seven_digits():
    assert md.Document("a&#99999999;b\n").html() == "<p>a&amp;#99999999;b</p>\n"   # eight: text, as the spec says
