# -*- coding: utf-8 -*-
"""A benchmark runs wherever the suite runs: it imports only the standard
library, pygim, its sibling modules in benchmarks/, and what pyproject.toml
declares — the runtime dependencies and the [test] extra. A module that is
merely installed on the author's machine is not declared."""

import ast
import functools
import importlib.metadata
import pathlib
import re
import sys

import pytest

from _pygim._inventory import installed
from pygim.pathlike import path

try:
    import tomllib
except ImportError:  # Python < 3.11: the [test] extra installs the backport
    import tomli as tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCHMARKS = sorted(pathlib.Path(str(p)) for p in path(str(ROOT / "benchmarks")).glob("*.py"))   # pygim's own walker
PROJECT = {"pygim", "_pygim"}
# The standard library's on a later Python than this one may be: tomllib from 3.11, the [test]
# extra's tomli standing in before it.
LATER_STDLIB = {"tomllib"}
pytestmark = pytest.mark.skipif(not BENCHMARKS, reason="benchmarks/ not present (testing the installed package)")


def _normal(distribution):
    """A distribution name as PEP 503 compares them: PyYAML and pyyaml are one."""
    return re.sub(r"[-_.]+", "-", distribution).lower()


@functools.cache
def _declared():
    pyproject = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    requirements = pyproject["tool"]["pygim"]["build"]["dependencies"] + pyproject["project"]["optional-dependencies"]["test"]
    return {_normal(re.match(r"[A-Za-z0-9._-]+", r).group()) for r in requirements}


def _imported(script):
    """The top-level modules `script` imports, relative imports left out."""
    tops = set()
    for node in ast.walk(ast.parse(script.read_text(encoding="utf-8"))):
        if isinstance(node, ast.Import):
            tops.update(alias.name.partition(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.level == 0:
            tops.add(node.module.partition(".")[0])
    return tops


@functools.cache
def _providers():
    """Each importable module's distribution, by pygim's inventory: one scan of every installed
    distribution, for all. It reads a distribution's RECORD where it has no top_level.txt, which
    importlib's packages_distributions() reads alone before 3.11 (click and pyarrow, on 3.10)."""
    return installed(importlib.metadata.distributions())


def _undeclared(script):
    """The third-party modules `script` imports that no declared distribution provides."""
    siblings = {p.stem for p in BENCHMARKS}
    third_party = _imported(script) - set(sys.stdlib_module_names) - LATER_STDLIB - PROJECT - siblings
    declared = _declared()
    providers = _providers()
    # a module nothing installed here provides is taken by its own name: tomli, declared for older Pythons
    return sorted(m for m in third_party if _normal(providers.get(m, m)) not in declared)


@pytest.mark.parametrize("benchmark", BENCHMARKS or [ROOT], ids=lambda p: p.name)
def test_benchmark_imports_only_declared_dependencies(benchmark):
    undeclared = _undeclared(benchmark)
    assert undeclared == [], (
        f"{benchmark.name} imports {undeclared}: neither pygim's runtime dependencies nor its [test] extra install it"
    )
