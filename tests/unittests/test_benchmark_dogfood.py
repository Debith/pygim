# -*- coding: utf-8 -*-
"""A benchmark eats pygim's own dogfood (global memory #20): where pygim ships a module's work, the
benchmark uses pygim's — unless the module is the reference pygim is measured against, or does what
pygim does not, and then its import says so in a comment. Debith, on PR #39 (2026-10-31): "Is this
for comparison purposes, or by accident? If former, then I think it would be good idea to add a
comment of the purpose of this import ... This comment applies all the benchmark files"."""

import pytest

from pygim import path
from pygim.code import parse_source

# What pygim ships in place of each module.
OWN = {
    "pathlib": "pygim.path and PathSet",
    "glob": "path.glob and PathSet",
    "fnmatch": "pygim.pathlike.match_pattern",
    "json": "path(...).read() and .write()",
    "yaml": "path(...).read() and .write()",
    "tomllib": "path(...).read()",
    "tomli": "path(...).read()",
    "ast": "pygim.code",
    "time": "pygim.utils.QuickTimer, on the monotonic clock (D83, Debith: \"I trust our tools are unbiased\")",
}

ROOT = path(__file__).resolve().parent.parent.parent
BENCHMARKS = (ROOT / "benchmarks").pathset("*.py", kind="file")   # none in a tree without benchmarks/
pytestmark = pytest.mark.skipif(not BENCHMARKS, reason="benchmarks/ not present (testing the installed package)")


def unexplained(name, text):
    """`file:line: module` for each import of a module pygim ships its own of, with no comment saying why."""
    lines = text.splitlines()
    tree = parse_source(text.encode("utf-8"), name)
    found = []
    for node in tree.find("Import"):
        found += [(node.lineno, alias.field("name").partition(".")[0]) for alias in node.field("names")]
    found += [(node.lineno, node.field("module").partition(".")[0]) for node in tree.find("ImportFrom")
              if node.field("module") and not node.field("level")]
    return [f"{name}:{line}: {module} (pygim ships {OWN[module]})" for line, module in found
            if module in OWN and "#" not in lines[line - 1]]


def test_the_check_finds_what_it_is_for():
    """Against its own hard case first: an import inside a string is no import, a comment is the reason."""
    script = 'import os\nimport pathlib\nimport json  # the reference\nCHILD = """\nimport ast\n"""\n'
    assert unexplained("probe.py", script) == ["probe.py:2: pathlib (pygim ships pygim.path and PathSet)"]


def test_a_benchmark_says_why_it_imports_what_pygim_ships():
    names = [p.name for p in BENCHMARKS]
    found = [hit for name, text in zip(names, BENCHMARKS.read_all_files()) for hit in unexplained(name, text)]
    assert found == [], "use pygim's own, or say on the import why this module (the reference, or what pygim lacks):\n" + "\n".join(found)
