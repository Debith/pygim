# -*- coding: utf-8 -*-
"""docs/examples: every example runs and ends with its OK line, and every argument callout stands
over its argument (docs/examples/README.md's conventions, #149). No CI step ran the examples, so
their asserts never ran there; a callout a column off pointed at the wrong argument."""

import os
import pathlib
import re
import runpy

import pytest

from pygim import code, pathlike

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs", "examples")
EXAMPLES = sorted(pathlib.Path(str(p)) for p in pathlike.path(os.path.normpath(ROOT)).pathset("*/example_*.py"))
IDS = [f"{p.parent.name}/{p.stem}" for p in EXAMPLES]


@pytest.mark.parametrize("example", EXAMPLES, ids=IDS)
def test_the_example_runs_to_its_ok_line(example, capsys):
    runpy.run_path(str(example), run_name="__main__")
    said = capsys.readouterr().out.strip().splitlines()
    assert said and " OK" in said[-1], said[-3:]


def argument_starts(code):
    """Where each argument of a call in `code` starts: after "(" or ", "."""
    return [k for k in range(1, len(code)) if code[k] not in " )" and (code[k - 1] == "(" or code[k - 2:k] == ", ")]


def misplaced_callouts(path):
    """Each `┌─ NAME:` callout (or `└─ NAME:` below its code) whose column, carried down by │ to
    its ▼ (up to its ▲), is not where its argument starts: `NAME=`, NAME itself, or the positional
    argument nearest. A callout of other words than `NAME:` points at what it says, unchecked."""
    lines = path.read_text(encoding="utf-8").split("\n")
    found = []
    for i, line in enumerate(lines):
        for m in re.finditer(r"([┌└])─ (\w+):", line):
            col, name = m.start(), m.group(2)
            step, marker = (1, "▼") if m.group(1) == "┌" else (-1, "▲")
            j = i + step
            while 0 <= j < len(lines) and lines[j][col:col + 1] == "│":
                j += step
            if not (0 <= j < len(lines)) or lines[j][col:col + 1] != marker:
                found.append(f"{path.name}:{i + 1}: {name}'s line does not reach a {marker} at column {col}")
                continue
            code_at = j + step
            while 0 <= code_at < len(lines) and lines[code_at].lstrip().startswith("#"):
                code_at += step
            code = lines[code_at] if 0 <= code_at < len(lines) else ""
            keyword = re.search(rf"\b{re.escape(name)}=", code)
            if keyword or code.startswith(name, col):
                want = keyword.start() if keyword else col
            else:
                starts = argument_starts(code)
                want = min(starts, key=lambda k: abs(k - col)) if starts else None
            if want != col:
                found.append(f"{path.name}:{i + 1}: {name} at column {col}, its argument at {want}: {code.strip()}")
    return found


@pytest.mark.parametrize("example", EXAMPLES, ids=IDS)
def test_each_argument_callout_stands_over_its_argument(example):
    assert misplaced_callouts(example) == []


@pytest.mark.parametrize("example", EXAMPLES, ids=IDS)
def test_each_line_is_under_a_hundred_characters(example):
    """An example is read before it is run (#149): 32 lines in 9 examples ran to 126 characters."""
    lines = example.read_text(encoding="utf-8").split("\n")
    assert [f"{n}: {len(line)}" for n, line in enumerate(lines, 1) if len(line) > 99] == []


@pytest.mark.parametrize("example", EXAMPLES, ids=IDS)
def test_each_assert_states_one_fact(example):
    """Debith on PR #37: "These asserts makes this file hard to read" — `assert a and b` is two facts
    a reader must take apart, and a failure names neither (#149). Found by pygim's own parser."""
    tree = code.parse_source(example.read_text(encoding="utf-8"), str(example))
    compound = [node.lineno for node in tree.find("Assert")
                if node.field("test").kind == "BoolOp" and node.field("test").field("op") == "And"]
    assert compound == []


def test_every_example_has_a_row_in_the_index():
    """docs/examples/README.md lists every example (pathlike's example_10 had no row until 2026-10-30)."""
    index = (pathlib.Path(ROOT) / "README.md").read_text(encoding="utf-8")
    assert [f"{p.parent.name}/{p.name}" for p in EXAMPLES if f"({p.parent.name}/{p.name})" not in index] == []

