# -*- coding: utf-8 -*-
"""The layers point one way (docs/definition_of_done.md): a core header is pybind-free.

Everything under src/_pygim_fast/mapping/ and the named core headers must build
without Python — that is what lets their laws be compile-time proofs. Only
adapter headers and bindings may include pybind11."""

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2] / "src" / "_pygim_fast"
# CI removes src/ before running the suite against the installed package: this
# is a source-tree check, so it skips there rather than fail.
pytestmark = pytest.mark.skipif(not ROOT.is_dir(), reason="source tree not present (testing the installed package)")
CORE = sorted(ROOT.glob("mapping/*.h")) + [
    ROOT / "utils" / "hash.h",
    ROOT / "utils" / "memory.h",
    ROOT / "pathlike" / "core.h",
    ROOT / "pathlike" / "uri.h",
    ROOT / "pathlike" / "path_table.h",
    ROOT / "pathlike" / "engine_list.h",
]
PYBIND = re.compile(r'#\s*include\s*[<"]pybind11/')


def _code(header):
    """The header's text with line comments removed (a comment may quote an include)."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in header.read_text(encoding="utf-8").splitlines())


@pytest.mark.parametrize("header", CORE or [ROOT], ids=lambda p: str(p.relative_to(ROOT.parent)))
def test_core_header_is_pybind_free(header):
    text = _code(header)
    assert not PYBIND.search(text), f"{header.relative_to(ROOT)} includes pybind11: core headers must not"
    assert "Py_" not in text and "PyObject" not in text, f"{header.relative_to(ROOT)} touches the Python C API"


def test_core_headers_include_only_core():
    """A core header may include the standard library and other core headers, nothing from adapter/."""
    for header in CORE:
        for inc in re.findall(r'#\s*include\s*"([^"]+)"', _code(header)):
            assert "adapter/" not in inc and "bindings" not in inc, f"{header.relative_to(ROOT)} includes {inc}"


def test_every_package_that_ships_holds_something():
    """A package of one empty __init__.py ships in every wheel and imports as if it offered
    something: pygim.ai did from 2025-08-07 until Debith dropped it (D78, 2026-10-31)."""
    src = ROOT.parent
    empty = []
    for package in ("pygim", "_pygim"):
        for init in sorted((src / package).rglob("__init__.py")):
            others = [p for p in init.parent.iterdir() if p.name not in ("__init__.py", "__pycache__")]
            if not init.read_text(encoding="utf-8").strip() and not others:
                empty.append(init.parent.relative_to(src).as_posix())
    assert empty == [], "a package holding nothing: give it its code, or remove it"
