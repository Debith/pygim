"""pathlike.markdown under hostile input: nothing crashes, nothing runs away.

A crash here is the process dying — a segfault or an abort leaves no exception
to catch — so every operation runs in a child process, on a 1 MB thread stack
(Windows' default; Linux gives 8 MB), and the child names each case before it
runs it: when the child dies, the last name it printed is the case that killed it.
"""

import json
import os
import subprocess
import sys
import time

import pytest

from pygim import pathlike

md = pathlike.markdown

# Inputs that once crashed the process or broke an invariant, by what they are.
HOSTILE = {
    "definitions then a short dash line (table check on an empty paragraph)": "[x]: /u\n-\n",
    "definitions then two dashes": "[x]: /u\n--\n",
    "definitions then a dash and a space": "[x]: /u\n- \n",
    "50,000 nested quotes": ">" * 50000 + " a\n",
    "50,000 nested list items": "- " * 50000 + "a\n",
    "5,000 nested quotes under a 1 MB stack": ">" * 5000 + " a\n",
    "a heading, a definition and a paragraph": "# h\n\n[d]: /u\n\npara\n",
}

# Run in the child: every public operation over every case, under both dialects.
CHILD = r"""
import json, sys, threading
from pygim import pathlike
md = pathlike.markdown
KINDS = [md.FrontMatter, md.Heading, md.Paragraph, md.Code, md.Html, md.ThematicBreak,
         md.Quote, md.List, md.Item, md.Table, md.Definition]
OWN = {k: [n for n, v in vars(k).items() if isinstance(v, property)] for k in KINDS}

def every_operation(doc):
    doc.html(); doc.plain; repr(doc); doc.sections
    for s in doc.sections:
        s.slug, s.lines, s.span, repr(s)
    walk = doc.walk()
    # Per-block calls on the first and the last blocks: on a 50,000-deep chain a
    # subtree's text is O(depth), so asking every block would be the test's own quadratic.
    for b in walk[:25] + walk[25:][-25:]:
        repr(b); b.plain; b.text; b.html(); b.lines; b.span; b.children; b.parent
        for name in OWN.get(type(b), []):
            getattr(b, name)
    for b in doc.blocks:
        doc.replace(b, b.text)
    # A kind's property asked of a block of another kind is a TypeError, never a read.
    for b in doc.blocks:
        for k in KINDS:
            if type(b) is k:
                continue
            for name in OWN[k]:
                try:
                    vars(k)[name].fget(b)
                except TypeError:
                    pass
                else:
                    raise AssertionError(f"{k.__name__}.{name} read a {type(b).__name__}")
    # Reassigning __class__ passes pybind's type check (the layouts match), so
    # the core itself refuses a heading's or a definition's index of another block.
    for i in range(min(len(doc.blocks), 3)):
        for k in KINDS:
            b = doc.blocks[i]
            if type(b) is k:
                continue
            b.__class__ = k
            for name in OWN[k]:
                try:
                    getattr(b, name)
                except ValueError:
                    pass

def run():
    for name, text in json.load(sys.stdin):
        for dialect in ("gfm", "commonmark"):
            print(f"{name} [{dialect}]", flush=True)
            every_operation(md.Document(text, dialect=dialect))
    print("done", flush=True)

threading.stack_size(1 << 20)
t = threading.Thread(target=run)
t.start(); t.join()
"""


def _run_child(cases):
    proc = subprocess.run([sys.executable, "-c", CHILD], input=json.dumps(cases), capture_output=True,
                          text=True, encoding="utf-8", timeout=120, env=os.environ.copy())
    lines = proc.stdout.splitlines()
    return proc.returncode, lines, proc.stderr


def test_no_hostile_input_crashes_any_operation():
    rc, lines, err = _run_child(sorted(HOSTILE.items()))
    died_at = lines[-1] if lines else "(before the first case)"
    assert rc == 0 and lines[-1:] == ["done"], f"the child died (rc={rc}) at: {died_at}\n{err[-2000:]}"


def test_a_kind_property_asked_of_another_kind_is_a_type_error():
    paragraph = md.Document("# h\n\npara\n").blocks[1]
    with pytest.raises(TypeError):
        md.Heading.title.fget(paragraph)


# --------------------------------------------------------------------------- #
# Nothing runs away: time grows with the input, not faster
# --------------------------------------------------------------------------- #
def _seconds(run, text, reps=3):
    best = float("inf")
    for _ in range(reps):
        t = time.perf_counter()
        run(text)
        best = min(best, time.perf_counter() - t)
    return best


def _sections(**kw):
    return lambda t: md.Document(t, **kw).sections


def _subsections(t):
    return [s.subsections for s in md.Document(t).sections]


# shape: (input at n, what to time, small n, big n). The two sizes are ~16x apart,
# so linear work grows ~16x and quadratic ~256x; allowing 3x the size ratio
# absorbs timing noise in a shared process without letting a quadratic through.
# An inline-heavy shape is measured against a baseline of the same lengths: its
# inline tree outgrows the CPU caches well before the input does (an inline node
# is ~120 bytes), so per-byte cost rises with size for nested and flat input alike,
# and only growth beyond the baseline's is the shape's own.
FLAT_EMPHASIS = lambda length: ("*a* " * (length // 4 + 1))[:length]
GROWTH = {
    "duplicate headings, github slugs": (lambda n: "## Usage\n\n" * n, _sections(), 100, 1600),
    "duplicate headings, toc slugs": (lambda n: "## Usage\n\n" * n, _sections(slugs="toc"), 100, 1600),
    "distinct headings": (lambda n: "".join(f"## h{i}\n\n" for i in range(n)), _sections(), 100, 1600),
    "explicit numbered toc headings": (lambda n: "".join(f"## a_{i % 7}\n\n" for i in range(n)), _sections(slugs="toc"), 100, 1600),
    "subsections of many sections": (lambda n: "# a\n\n" + "## b\n\n" * n, _subsections, 100, 1600),
    "one line of nested bullets": (lambda n: "- " * n + "a\n", md.Document, 1000, 16000),
    "one line of nested stars": (lambda n: "* " * n + "a\n", md.Document, 1000, 16000),
    "unmatched long backtick runs": (lambda n: "".join("`" * (64 + i) + "!" for i in range(n)), lambda t: md.Document(t).plain, 20, 160),
    "backtick runs of every length": (lambda n: "".join("e" + "`" * i for i in range(1, n)), lambda t: md.Document(t).html(), 50, 200),
    "nested emphasis": (lambda n: "*a **a " * n + "b** b*" * n, lambda t: md.Document(t).html(), 250, 4000, FLAT_EMPHASIS),
    "nested quotes": (lambda n: "> " * n + "a", lambda t: md.Document(t).html(), 500, 8000),
    "unclosed links": (lambda n: "[a](b" * n, lambda t: md.Document(t).html(), 500, 8000),
    "unclosed comments": (lambda n: "</" + "<!--" * n, lambda t: md.Document(t).html(), 500, 8000),
    "image link openers": (lambda n: "![[]()" * n, lambda t: md.Document(t).html(), 500, 8000),
    "nested brackets": (lambda n: "[" * n + "a" + "]" * n, lambda t: md.Document(t).html(), 500, 8000),
}


@pytest.mark.parametrize("shape", sorted(GROWTH))
def test_time_grows_no_faster_than_the_input(shape):
    make, run, n_small, n_big, *baseline = GROWTH[shape]
    small, big = make(n_small), make(n_big)
    run(small)   # warm: first-use caches and allocations
    t_small, t_big = _seconds(run, small), _seconds(run, big)
    ratio = len(big) / len(small)
    if baseline:   # what linear work of this kind costs here at these sizes
        b_small, b_big = baseline[0](len(small)), baseline[0](len(big))
        ratio = max(ratio, _seconds(run, b_big) / _seconds(run, b_small))
    assert t_big <= 3 * ratio * t_small + 0.001, (
        f"{shape}: {len(small)} -> {len(big)} chars took {t_small * 1e3:.2f} -> {t_big * 1e3:.2f} ms "
        f"({t_big / t_small:.0f}x where linear work grows {ratio:.0f}x)")


def test_document_text_is_built_once():
    d = md.Document("# a\n" * 1000)
    assert d.text is d.text   # a span slices it: rebuilding it per access made each slice O(n)


UNINITIALISED = r"""
from pygim import pathlike
md = pathlike.markdown
for cls in [md.Document, md.Block, md.Section, md.Heading]:
    obj = cls.__new__(cls)   # __init__ never runs
    for name in [n for n in dir(cls) if not n.startswith("_")] + ["__repr__"]:
        try:
            attr = getattr(obj, name)
            if callable(attr):
                attr()
        except Exception:
            pass
print("done")
"""


@pytest.mark.xfail(strict=True, reason=(
    "pybind11 gives a method of an instance whose __init__ never ran uninitialised memory "
    "(type_caster_base.h, lazy allocation), so the process dies. Every pygim class does this "
    "(pathlike.path too); the fix belongs to the adapter layer as a whole, not to markdown alone."))
def test_an_instance_whose_init_never_ran_raises_instead_of_crashing():
    proc = subprocess.run([sys.executable, "-c", UNINITIALISED], capture_output=True, text=True, timeout=60,
                          env=os.environ.copy())
    assert proc.returncode == 0 and proc.stdout.strip().endswith("done")
