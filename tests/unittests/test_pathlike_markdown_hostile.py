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
